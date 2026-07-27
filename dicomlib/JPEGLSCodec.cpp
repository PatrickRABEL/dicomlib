#include "JPEGLSCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_JPEGLS
#include <charls/charls.h>
#include <string>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_JPEGLS
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
				"JPEG-LS support requires 1 or 3 samples per pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"JPEG-LS support requires 8-bit or 16-bit Pixel Data");
			Enforce(geometry.bitsStored >= 2 && geometry.bitsStored <= geometry.bitsAllocated,
				"JPEG-LS Bits Stored is inconsistent with Bits Allocated");
			Enforce(geometry.pixelRepresentation <= 1, "JPEG-LS Pixel Representation must be 0 or 1");
			Enforce(geometry.samplesPerPixel == 1 || geometry.planarConfiguration == 0,
				"JPEG-LS RGB encode/decode support requires Planar Configuration 0");
			return geometry;
		}

		std::vector<BYTE> ConcatenateFragments(const DataSet& data)
		{
			std::vector<BYTE> codestream;
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "JPEG-LS Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "JPEG-LS fragments must be OB");
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

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const Value& value = data(TAG_PIXEL_DATA);
				Enforce(value.vr() == VR_OB, "8-bit JPEG-LS Pixel Data must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OW, "16-bit JPEG-LS Pixel Data must be OW");
			const std::vector<UINT16>& words = value.Get<TypeFromVR<VR_OW>::Type>();
			std::vector<BYTE> bytes(words.size() * 2, 0);
			for(size_t i=0;i<words.size();++i)
			{
				bytes[i * 2] = BYTE(words[i] & 0xff);
				bytes[i * 2 + 1] = BYTE((words[i] >> 8) & 0xff);
			}
			return bytes;
		}

		charls::interleave_mode InterleaveMode(const ImageGeometry& geometry)
		{
			return geometry.samplesPerPixel == 1 ? charls::interleave_mode::none : charls::interleave_mode::sample;
		}

		std::vector<BYTE> EncodeJPEGLS(const std::vector<BYTE>& pixels, const ImageGeometry& geometry)
		{
			const size_t bytesPerSample = geometry.bitsAllocated / 8;
			const size_t expectedSize = size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * bytesPerSample;
			Enforce(pixels.size() == expectedSize,
				"Native Pixel Data size is inconsistent with JPEG-LS image attributes");

			charls::frame_info frame = {geometry.columns, geometry.rows, geometry.bitsStored, geometry.samplesPerPixel};
			std::vector<BYTE> encoded;
			try
			{
				encoded = charls::jpegls_encoder::encode(
					pixels,
					frame,
					InterleaveMode(geometry),
					charls::encoding_options::even_destination_size);
			}
			catch(const charls::jpegls_error& error)
			{
				throw exception(std::string("Failed to encode JPEG-LS Pixel Data: ") + error.what());
			}
			return encoded;
		}

		std::vector<BYTE> DecodeJPEGLS(const std::vector<BYTE>& codestream, const ImageGeometry& geometry)
		{
			Enforce(!codestream.empty(), "JPEG-LS codestream is empty");
			std::vector<BYTE> pixels;
			try
			{
				charls::jpegls_decoder decoder(codestream, true);
				const charls::frame_info& frame = decoder.frame_info();
				Enforce(frame.width == geometry.columns, "JPEG-LS width does not match DICOM Columns");
				Enforce(frame.height == geometry.rows, "JPEG-LS height does not match DICOM Rows");
				Enforce(frame.component_count == geometry.samplesPerPixel,
					"JPEG-LS component count does not match Samples per Pixel");
				Enforce(frame.bits_per_sample <= geometry.bitsAllocated,
					"JPEG-LS component precision exceeds DICOM Bits Allocated");
				Enforce(decoder.near_lossless() == 0,
					"JPEG-LS Lossless Transfer Syntax requires NEAR 0");
				Enforce(decoder.interleave_mode() == InterleaveMode(geometry),
					"JPEG-LS interleave mode is not supported for the DICOM image layout");
				pixels.resize(decoder.destination_size());
				decoder.decode(pixels);
			}
			catch(const charls::jpegls_error& error)
			{
				throw exception(std::string("Failed to decode JPEG-LS Pixel Data: ") + error.what());
			}
			return pixels;
		}
#endif
	}

	void DecodeJPEGLSLosslessPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_JPEGLS
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeJPEGLS(codestream, geometry);
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
		throw exception("JPEG-LS requires DICOMLIB_WITH_JPEGLS");
#endif
	}

	DataSet EncodeJPEGLSLosslessPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_JPEGLS
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeJPEGLS(pixels, geometry);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("JPEG-LS requires DICOMLIB_WITH_JPEGLS");
#endif
	}
}
