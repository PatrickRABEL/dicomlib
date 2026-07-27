#include "HTJ2KCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_HTJ2K
#include <openjph/ojph_base.h>
#include <openjph/ojph_codestream.h>
#include <openjph/ojph_file.h>
#include <openjph/ojph_mem.h>
#include <openjph/ojph_params.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_HTJ2K
		struct ImageGeometry
		{
			UINT16 rows;
			UINT16 columns;
			UINT16 samplesPerPixel;
			UINT16 bitsAllocated;
			UINT16 bitsStored;
			UINT16 pixelRepresentation;
			UINT16 planarConfiguration;
		};

		ImageGeometry ReadImageGeometry(const DataSet& data)
		{
			ImageGeometry geometry = {0, 0, 0, 0, 0, 0, 0};
			data(TAG_ROWS) >> geometry.rows;
			data(TAG_COLUMNS) >> geometry.columns;
			data(TAG_SAMPLES_PER_PX) >> geometry.samplesPerPixel;
			data(TAG_BITS_ALLOC) >> geometry.bitsAllocated;
			data(TAG_BITS_STORED) >> geometry.bitsStored;
			data(TAG_PX_REPRESENT) >> geometry.pixelRepresentation;
			if(geometry.samplesPerPixel > 1)
				data(TAG_PLANAR_CONFIG) >> geometry.planarConfiguration;
			Enforce(geometry.samplesPerPixel == 1 || geometry.samplesPerPixel == 3,
				"HTJ2K support requires 1 or 3 samples per pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"HTJ2K support requires 8-bit or 16-bit Pixel Data");
			Enforce(geometry.bitsStored > 0 && geometry.bitsStored <= geometry.bitsAllocated,
				"HTJ2K Bits Stored is inconsistent with Bits Allocated");
			Enforce(geometry.pixelRepresentation == 0,
				"HTJ2K support requires unsigned Pixel Data");
			Enforce(geometry.samplesPerPixel == 1 || geometry.planarConfiguration == 0,
				"HTJ2K RGB encode/decode support requires Planar Configuration 0");
			return geometry;
		}

		std::vector<BYTE> ConcatenateFragments(const DataSet& data)
		{
			std::vector<BYTE> codestream;
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "HTJ2K Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "HTJ2K fragments must be OB");
				const std::vector<BYTE>& fragment = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
				codestream.insert(codestream.end(), fragment.begin(), fragment.end());
			}
			return codestream;
		}

		DataSet CopyWithoutPixelData(const DataSet& data)
		{
			DataSet copy;
			for(DataSet::const_iterator i=data.begin();i!=data.end();++i)
			{
				if(i->first != TAG_PIXEL_DATA)
					copy.insert(*i);
			}
			return copy;
		}

		void PutSingleStringValue(DataSet& data, Tag tag, VR vr, const std::string& value)
		{
			data.erase(tag);
			if(vr == VR_CS)
				data.Put<VR_CS>(tag, value);
			else if(vr == VR_DS)
				data.Put<VR_DS>(tag, value);
			else
				throw exception("Unsupported HTJ2K metadata VR");
		}

		std::string CompressionRatioString(size_t nativeSize, size_t encodedSize)
		{
			Enforce(encodedSize != 0, "HTJ2K encoded Pixel Data is empty");
			std::ostringstream ratio;
			ratio << std::setprecision(6) << (double(nativeSize) / double(encodedSize));
			return ratio.str();
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const Value& value = data(TAG_PIXEL_DATA);
				Enforce(value.vr() == VR_OB, "8-bit HTJ2K Pixel Data must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OW, "16-bit HTJ2K Pixel Data must be OW");
			const std::vector<UINT16>& words = value.Get<TypeFromVR<VR_OW>::Type>();
			std::vector<BYTE> bytes(words.size() * 2, 0);
			for(size_t i=0;i<words.size();++i)
			{
				bytes[i * 2] = BYTE(words[i] & 0xff);
				bytes[i * 2 + 1] = BYTE((words[i] >> 8) & 0xff);
			}
			return bytes;
		}

		size_t ExpectedNativeSize(const ImageGeometry& geometry)
		{
			return size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * size_t(geometry.bitsAllocated / 8);
		}

		int ResolutionCount(const ImageGeometry& geometry)
		{
			UINT16 dimension = std::min(geometry.rows, geometry.columns);
			int resolutions = 1;
			while(dimension >= 2 && resolutions < 6)
			{
				dimension = UINT16(dimension / 2);
				++resolutions;
			}
			return resolutions;
		}

		int RPCLResolutionCount(const ImageGeometry& geometry)
		{
			UINT16 dimension = std::max(geometry.rows, geometry.columns);
			int resolutions = 1;
			while(dimension > 64)
			{
				dimension = UINT16((dimension + 1) / 2);
				++resolutions;
			}
			return resolutions;
		}

		int ReadSample(const std::vector<BYTE>& pixels, const ImageGeometry& geometry, size_t row, size_t column, size_t component)
		{
			const size_t sampleIndex =
				(row * size_t(geometry.columns) + column) * size_t(geometry.samplesPerPixel) + component;
			if(geometry.bitsAllocated == 8)
				return pixels[sampleIndex];
			const size_t offset = sampleIndex * 2;
			return UINT16(pixels[offset]) | (UINT16(pixels[offset + 1]) << 8);
		}

		void WriteSample(std::vector<BYTE>& pixels, const ImageGeometry& geometry, size_t row, size_t column, size_t component, int value)
		{
			const size_t sampleIndex =
				(row * size_t(geometry.columns) + column) * size_t(geometry.samplesPerPixel) + component;
			if(geometry.bitsAllocated == 8)
			{
				pixels[sampleIndex] = BYTE(value & 0xff);
				return;
			}
			const size_t offset = sampleIndex * 2;
			pixels[offset] = BYTE(value & 0xff);
			pixels[offset + 1] = BYTE((value >> 8) & 0xff);
		}

		std::vector<BYTE> EncodeHTJ2K(
			const std::vector<BYTE>& pixels,
			const ImageGeometry& geometry,
			bool rpcl,
			bool reversible,
			UINT16 qfactor)
		{
			Enforce(pixels.size() == ExpectedNativeSize(geometry),
				"Native Pixel Data size is inconsistent with HTJ2K image attributes");
			if(!reversible)
				Enforce(qfactor >= 1 && qfactor <= 100,
					"HTJ2K lossy Qfactor must be between 1 and 100");

			ojph::codestream codestream;
			codestream.set_planar(true);
			if(rpcl)
			{
				codestream.request_tlm_marker(true);
				codestream.set_tilepart_divisions(true, false);
			}
			ojph::param_siz siz = codestream.access_siz();
			siz.set_image_extent(ojph::point(geometry.columns, geometry.rows));
			siz.set_image_offset(ojph::point(0, 0));
			siz.set_tile_size(ojph::size(geometry.columns, geometry.rows));
			siz.set_tile_offset(ojph::point(0, 0));
			siz.set_num_components(geometry.samplesPerPixel);
			for(ojph::ui32 component=0;component<geometry.samplesPerPixel;++component)
				siz.set_component(component, ojph::point(1, 1), geometry.bitsStored, false);

			ojph::param_cod cod = codestream.access_cod();
			cod.set_num_decomposition(static_cast<ojph::ui32>(
				(rpcl ? RPCLResolutionCount(geometry) : ResolutionCount(geometry)) - 1));
			cod.set_block_dims(64, 64);
			cod.set_progression_order(rpcl ? "RPCL" : "LRCP");
			cod.set_color_transform(false);
			cod.set_reversible(reversible);
			if(!reversible)
			{
				ojph::param_qcd qcd = codestream.access_qcd();
				qcd.set_qfactor(static_cast<ojph::ui8>(qfactor));
			}

			ojph::mem_outfile output;
			output.open(65536, false);
			std::vector<BYTE> encoded;
			try
			{
				codestream.write_headers(&output);
				std::vector<size_t> rows(geometry.samplesPerPixel, 0);
				ojph::ui32 component = 0;
				ojph::line_buf* line = 0;
				const size_t lineCount = size_t(geometry.rows) * size_t(geometry.samplesPerPixel);
				for(size_t lineIndex=0;lineIndex<lineCount;++lineIndex)
				{
					line = codestream.exchange(line, component);
					Enforce(line != 0, "HTJ2K encoder finished before consuming all rows");
					Enforce(component < geometry.samplesPerPixel, "HTJ2K encoder requested an invalid component");
					Enforce(line->size >= geometry.columns, "HTJ2K encoder line is shorter than Columns");
					Enforce(rows[component] < geometry.rows, "HTJ2K encoder requested too many rows");
					for(size_t column=0;column<geometry.columns;++column)
					{
						const int sample = ReadSample(pixels, geometry, rows[component], column, component);
						if(line->flags & ojph::line_buf::LFT_INTEGER)
							line->i32[column] = sample;
						else
							line->f32[column] = static_cast<float>(sample);
					}
					++rows[component];
				}
				(void)codestream.exchange(line, component);
				for(size_t componentIndex=0;componentIndex<rows.size();++componentIndex)
					Enforce(rows[componentIndex] == geometry.rows, "HTJ2K encoder did not consume all rows");
				codestream.flush();
				encoded.assign(output.get_data(), output.get_data() + output.tell());
				codestream.close();
			}
			catch(...)
			{
				throw exception("Failed to encode HTJ2K Pixel Data");
			}

			if(encoded.size() & 1)
				encoded.push_back(0);
			return encoded;
		}

		int ClampSample(int value, const ImageGeometry& geometry)
		{
			const int maximum = (1 << geometry.bitsStored) - 1;
			return std::max(0, std::min(maximum, value));
		}

		std::vector<BYTE> DecodeHTJ2K(
			const std::vector<BYTE>& encoded,
			const ImageGeometry& geometry,
			bool requireReversible,
			bool requireRPCL)
		{
			Enforce(!encoded.empty(), "HTJ2K codestream is empty");
			ojph::mem_infile input;
			input.open(encoded.data(), encoded.size());
			ojph::codestream codestream;
			std::vector<BYTE> pixels(ExpectedNativeSize(geometry), 0);
			try
			{
				codestream.read_headers(&input);
				ojph::param_siz siz = codestream.access_siz();
				Enforce(siz.get_image_extent().x == geometry.columns, "HTJ2K width does not match DICOM Columns");
				Enforce(siz.get_image_extent().y == geometry.rows, "HTJ2K height does not match DICOM Rows");
				Enforce(siz.get_num_components() == geometry.samplesPerPixel,
					"HTJ2K component count does not match Samples per Pixel");
				for(ojph::ui32 component=0;component<geometry.samplesPerPixel;++component)
				{
					Enforce(siz.get_bit_depth(component) <= geometry.bitsAllocated,
						"HTJ2K component precision exceeds DICOM Bits Allocated");
					Enforce(!siz.is_signed(component), "Signed HTJ2K Pixel Data is not supported");
					Enforce(siz.get_downsampling(component).x == 1 && siz.get_downsampling(component).y == 1,
						"Subsampled HTJ2K components are not supported");
				}
				ojph::param_cod cod = codestream.access_cod();
				if(requireReversible)
					Enforce(cod.is_reversible(), "HTJ2K Lossless Transfer Syntax requires reversible coding");
				if(requireRPCL)
				{
					Enforce(std::string(cod.get_progression_order_as_string()) == "RPCL",
						"HTJ2K Lossless RPCL Transfer Syntax requires RPCL progression order");
					const ojph::ui32 decompositions = cod.get_num_decompositions();
					const size_t dimension = std::max<UINT16>(geometry.rows, geometry.columns);
					const size_t divisor = size_t(1) << decompositions;
					Enforce(((dimension + divisor - 1) / divisor) <= 64,
						"HTJ2K Lossless RPCL Transfer Syntax requires base resolution width or height <= 64");
				}

				codestream.create();
				std::vector<size_t> rows(geometry.samplesPerPixel, 0);
				ojph::ui32 component = 0;
				ojph::line_buf* line = 0;
				const size_t lineCount = size_t(geometry.rows) * size_t(geometry.samplesPerPixel);
				for(size_t lineIndex=0;lineIndex<lineCount;++lineIndex)
				{
					line = codestream.pull(component);
					Enforce(line != 0, "HTJ2K decoder finished before producing all rows");
					Enforce(component < geometry.samplesPerPixel, "HTJ2K decoder returned an invalid component");
					Enforce(line->size >= geometry.columns, "HTJ2K decoder line is shorter than Columns");
					Enforce(rows[component] < geometry.rows, "HTJ2K decoder returned too many rows");
					for(size_t column=0;column<geometry.columns;++column)
					{
						int sample = 0;
						if(line->flags & ojph::line_buf::LFT_INTEGER)
							sample = line->i32[column];
						else
							sample = static_cast<int>(std::lround(line->f32[column]));
						WriteSample(pixels, geometry, rows[component], column, component, ClampSample(sample, geometry));
					}
					++rows[component];
				}
				for(size_t componentIndex=0;componentIndex<rows.size();++componentIndex)
					Enforce(rows[componentIndex] == geometry.rows, "HTJ2K decoder did not produce all rows");
				codestream.close();
			}
			catch(...)
			{
				throw exception("Failed to decode HTJ2K Pixel Data");
			}
			return pixels;
		}
#endif
	}

	void DecodeHTJ2KLosslessPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeHTJ2K(codestream, geometry, true, false);
		data.erase(TAG_PIXEL_DATA);
		if(geometry.bitsAllocated == 8)
			data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
		else
		{
			std::vector<UINT16> words(pixels.size() / 2, 0);
			for(size_t i=0;i<words.size();++i)
				words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
			data.Put<VR_OW>(TAG_PIXEL_DATA, words);
		}
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}

	void DecodeHTJ2KRPCLLosslessPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeHTJ2K(codestream, geometry, true, true);
		data.erase(TAG_PIXEL_DATA);
		if(geometry.bitsAllocated == 8)
			data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
		else
		{
			std::vector<UINT16> words(pixels.size() / 2, 0);
			for(size_t i=0;i<words.size();++i)
				words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
			data.Put<VR_OW>(TAG_PIXEL_DATA, words);
		}
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}

	void DecodeHTJ2KPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeHTJ2K(codestream, geometry, false, false);
		data.erase(TAG_PIXEL_DATA);
		if(geometry.bitsAllocated == 8)
			data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
		else
		{
			std::vector<UINT16> words(pixels.size() / 2, 0);
			for(size_t i=0;i<words.size();++i)
				words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
			data.Put<VR_OW>(TAG_PIXEL_DATA, words);
		}
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}

	DataSet EncodeHTJ2KLosslessPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeHTJ2K(pixels, geometry, false, true, 0);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}

	DataSet EncodeHTJ2KRPCLLosslessPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeHTJ2K(pixels, geometry, true, true, 0);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}

	DataSet EncodeHTJ2KPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_HTJ2K
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded =
			EncodeHTJ2K(pixels, geometry, false, false, static_cast<UINT16>(DICOMLIB_HTJ2K_QFACTOR));
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION, VR_CS, "01");
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_RATIO, VR_DS,
			CompressionRatioString(pixels.size(), encoded.size()));
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_METHOD, VR_CS, "ISO_15444_15");
		return encodedData;
#else
		(void)data;
		throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
	}
}
