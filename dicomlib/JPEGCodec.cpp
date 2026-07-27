#include "JPEGCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_JPEG
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#ifndef XMD_H
#define XMD_H
#define DICOMLIB_DEFINED_XMD_H
#endif
#include <jpeglib.h>
#ifdef DICOMLIB_DEFINED_XMD_H
#undef XMD_H
#undef DICOMLIB_DEFINED_XMD_H
#endif
#include <sstream>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_JPEG
		void ReadImageGeometry(
			const DataSet& data,
			UINT16& rows,
			UINT16& columns,
			UINT16& samplesPerPixel,
			UINT16& bitsAllocated)
		{
			data(TAG_ROWS) >> rows;
			data(TAG_COLUMNS) >> columns;
			data(TAG_SAMPLES_PER_PX) >> samplesPerPixel;
			data(TAG_BITS_ALLOC) >> bitsAllocated;
		}

		std::vector<BYTE> ConcatenateFragments(const DataSet& data)
		{
			std::vector<BYTE> codestream;
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "JPEG Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "JPEG fragments must be OB");
				const std::vector<BYTE>& fragment = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
				codestream.insert(codestream.end(), fragment.begin(), fragment.end());
			}
			return codestream;
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data)
		{
			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OB, "JPEG Baseline Pixel Data must be 8-bit OB");
			return value.Get<TypeFromVR<VR_OB>::Type>();
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
				throw exception("Unsupported JPEG metadata VR");
		}

		std::string CompressionRatioString(size_t nativeSize, size_t encodedSize)
		{
			Enforce(encodedSize != 0, "JPEG encoded Pixel Data is empty");
			std::ostringstream ratio;
			ratio << std::setprecision(6) << (double(nativeSize) / double(encodedSize));
			return ratio.str();
		}

		struct JPEGErrorManager
		{
			jpeg_error_mgr pub;
			jmp_buf jumpBuffer;
		};

		void JPEGErrorExit(j_common_ptr cinfo)
		{
			JPEGErrorManager* manager = reinterpret_cast<JPEGErrorManager*>(cinfo->err);
			longjmp(manager->jumpBuffer, 1);
		}

		std::vector<BYTE> DecodeJPEG(
			const std::vector<BYTE>& codestream,
			UINT16 rows,
			UINT16 columns,
			UINT16 samplesPerPixel)
		{
			Enforce(!codestream.empty(), "JPEG codestream is empty");

			jpeg_decompress_struct cinfo;
			JPEGErrorManager errorManager;
			cinfo.err = jpeg_std_error(&errorManager.pub);
			errorManager.pub.error_exit = JPEGErrorExit;

			if(setjmp(errorManager.jumpBuffer))
			{
				jpeg_destroy_decompress(&cinfo);
				throw exception("Failed to decode JPEG Baseline Pixel Data");
			}

			jpeg_create_decompress(&cinfo);
			jpeg_mem_src(&cinfo, const_cast<unsigned char*>(codestream.data()), codestream.size());
			jpeg_read_header(&cinfo, TRUE);
			jpeg_start_decompress(&cinfo);

			Enforce(cinfo.output_width == columns, "JPEG width does not match DICOM Columns");
			Enforce(cinfo.output_height == rows, "JPEG height does not match DICOM Rows");
			Enforce(cinfo.output_components == samplesPerPixel, "JPEG component count does not match Samples per Pixel");

			const size_t rowStride = size_t(cinfo.output_width) * size_t(cinfo.output_components);
			std::vector<BYTE> pixels(rowStride * size_t(cinfo.output_height), 0);
			while(cinfo.output_scanline < cinfo.output_height)
			{
				BYTE* row = pixels.data() + (size_t(cinfo.output_scanline) * rowStride);
				jpeg_read_scanlines(&cinfo, &row, 1);
			}

			jpeg_finish_decompress(&cinfo);
			jpeg_destroy_decompress(&cinfo);
			return pixels;
		}

		std::vector<BYTE> EncodeJPEG(
			const std::vector<BYTE>& pixels,
			UINT16 rows,
			UINT16 columns,
			UINT16 samplesPerPixel)
		{
			jpeg_compress_struct cinfo;
			JPEGErrorManager errorManager;
			cinfo.err = jpeg_std_error(&errorManager.pub);
			errorManager.pub.error_exit = JPEGErrorExit;

			if(setjmp(errorManager.jumpBuffer))
			{
				jpeg_destroy_compress(&cinfo);
				throw exception("Failed to encode JPEG Baseline Pixel Data");
			}

			jpeg_create_compress(&cinfo);
			unsigned char* output = 0;
			unsigned long outputSize = 0;
			jpeg_mem_dest(&cinfo, &output, &outputSize);

			cinfo.image_width = columns;
			cinfo.image_height = rows;
			cinfo.input_components = samplesPerPixel;
			cinfo.in_color_space = (samplesPerPixel == 1) ? JCS_GRAYSCALE : JCS_RGB;
			jpeg_set_defaults(&cinfo);
			jpeg_set_quality(&cinfo, 90, TRUE);
			jpeg_start_compress(&cinfo, TRUE);

			const size_t rowStride = size_t(columns) * size_t(samplesPerPixel);
			while(cinfo.next_scanline < cinfo.image_height)
			{
				JSAMPROW row = const_cast<JSAMPROW>(pixels.data() + (size_t(cinfo.next_scanline) * rowStride));
				jpeg_write_scanlines(&cinfo, &row, 1);
			}

			jpeg_finish_compress(&cinfo);
			std::vector<BYTE> encoded(output, output + outputSize);
			jpeg_destroy_compress(&cinfo);
			std::free(output);
			if(encoded.size() & 1)
				encoded.push_back(0);
			return encoded;
		}
#endif
	}

	void DecodeJPEGBaselinePixelData(DataSet& data)
	{
#if DICOMLIB_WITH_JPEG
		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		ReadImageGeometry(data, rows, columns, samplesPerPixel, bitsAllocated);
		Enforce(bitsAllocated == 8, "JPEG Baseline support is limited to 8-bit Pixel Data");
		Enforce(samplesPerPixel == 1 || samplesPerPixel == 3, "JPEG Baseline support requires 1 or 3 samples per pixel");

		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeJPEG(codestream, rows, columns, samplesPerPixel);
		data.erase(TAG_PIXEL_DATA);
		data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
#else
		(void)data;
		throw exception("JPEG Baseline requires DICOMLIB_WITH_JPEG");
#endif
	}

	DataSet EncodeJPEGBaselinePixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_JPEG
		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		ReadImageGeometry(data, rows, columns, samplesPerPixel, bitsAllocated);
		Enforce(bitsAllocated == 8, "JPEG Baseline support is limited to 8-bit Pixel Data");
		Enforce(samplesPerPixel == 1 || samplesPerPixel == 3, "JPEG Baseline support requires 1 or 3 samples per pixel");

		const std::vector<BYTE> pixels = NativePixelBytes(data);
		Enforce(pixels.size() == size_t(rows) * size_t(columns) * size_t(samplesPerPixel),
			"Native Pixel Data size is inconsistent with JPEG image attributes");

		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeJPEG(pixels, rows, columns, samplesPerPixel);
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION, VR_CS, "01");
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_RATIO, VR_DS,
			CompressionRatioString(pixels.size(), encoded.size()));
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_METHOD, VR_CS, "ISO_10918_1");
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("JPEG Baseline requires DICOMLIB_WITH_JPEG");
#endif
	}
}
