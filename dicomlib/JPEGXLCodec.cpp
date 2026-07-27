#include "JPEGXLCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_JPEGXL
#include <jxl/decode.h>
#include <jxl/encode.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_JPEGXL
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

		struct EncoderDeleter
		{
			void operator()(JxlEncoder* encoder) const
			{
				if(encoder)
					JxlEncoderDestroy(encoder);
			}
		};

		struct DecoderDeleter
		{
			void operator()(JxlDecoder* decoder) const
			{
				if(decoder)
					JxlDecoderDestroy(decoder);
			}
		};

		typedef std::unique_ptr<JxlEncoder, EncoderDeleter> EncoderPtr;
		typedef std::unique_ptr<JxlDecoder, DecoderDeleter> DecoderPtr;

		void EnforceJxlEncoder(JxlEncoderStatus status, const char* context)
		{
			Enforce(status == JXL_ENC_SUCCESS, context);
		}

		void EnforceJxlDecoder(JxlDecoderStatus status, const char* context)
		{
			Enforce(status == JXL_DEC_SUCCESS, context);
		}

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
				"JPEG XL support requires 1 or 3 samples per pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"JPEG XL support requires 8-bit or 16-bit Pixel Data");
			Enforce(geometry.bitsStored > 0 && geometry.bitsStored <= geometry.bitsAllocated,
				"JPEG XL Bits Stored is inconsistent with Bits Allocated");
			Enforce(geometry.pixelRepresentation == 0,
				"JPEG XL support requires unsigned Pixel Data");
			Enforce(geometry.samplesPerPixel == 1 || geometry.planarConfiguration == 0,
				"JPEG XL RGB encode/decode support requires Planar Configuration 0");
			return geometry;
		}

		std::vector<BYTE> ConcatenateFragments(const DataSet& data)
		{
			std::vector<BYTE> codestream;
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "JPEG XL Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "JPEG XL fragments must be OB");
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
				throw exception("Unsupported JPEG XL metadata VR");
		}

		std::string CompressionRatioString(size_t nativeSize, size_t encodedSize)
		{
			Enforce(encodedSize != 0, "JPEG XL encoded Pixel Data is empty");
			std::ostringstream ratio;
			ratio << std::setprecision(6) << (double(nativeSize) / double(encodedSize));
			return ratio.str();
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const Value& value = data(TAG_PIXEL_DATA);
				Enforce(value.vr() == VR_OB, "8-bit JPEG XL Pixel Data must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OW, "16-bit JPEG XL Pixel Data must be OW");
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

		JxlPixelFormat PixelFormat(const ImageGeometry& geometry)
		{
			JxlPixelFormat format = {
				static_cast<uint32_t>(geometry.samplesPerPixel),
				geometry.bitsAllocated == 8 ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16,
				JXL_LITTLE_ENDIAN,
				0
			};
			return format;
		}

		std::vector<BYTE> EncodeJPEGXL(
			const std::vector<BYTE>& pixels,
			const ImageGeometry& geometry,
			bool lossless,
			float distance)
		{
			Enforce(pixels.size() == ExpectedNativeSize(geometry),
				"Native Pixel Data size is inconsistent with JPEG XL image attributes");
			if(!lossless)
				Enforce(distance > 0.0f && distance <= 25.0f,
					"JPEG XL lossy distance must be greater than 0 and less than or equal to 25");

			EncoderPtr encoder(JxlEncoderCreate(0));
			Enforce(encoder.get() != 0, "Failed to create JPEG XL encoder");
			EnforceJxlEncoder(JxlEncoderUseContainer(encoder.get(), JXL_FALSE),
				"Failed to configure JPEG XL codestream output");

			JxlBasicInfo info;
			JxlEncoderInitBasicInfo(&info);
			info.xsize = geometry.columns;
			info.ysize = geometry.rows;
			info.bits_per_sample = geometry.bitsStored;
			info.exponent_bits_per_sample = 0;
			info.uses_original_profile = JXL_TRUE;
			info.num_color_channels = geometry.samplesPerPixel;
			info.num_extra_channels = 0;
			EnforceJxlEncoder(JxlEncoderSetBasicInfo(encoder.get(), &info),
				"Failed to set JPEG XL basic information");

			JxlColorEncoding color;
			JxlColorEncodingSetToSRGB(&color, geometry.samplesPerPixel == 1 ? JXL_TRUE : JXL_FALSE);
			EnforceJxlEncoder(JxlEncoderSetColorEncoding(encoder.get(), &color),
				"Failed to set JPEG XL color encoding");

			JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder.get(), 0);
			Enforce(settings != 0, "Failed to create JPEG XL frame settings");
			const JxlBitDepth bitDepth = {JXL_BIT_DEPTH_FROM_CODESTREAM, 0, 0};
			EnforceJxlEncoder(JxlEncoderSetFrameBitDepth(settings, &bitDepth),
				"Failed to set JPEG XL input bit depth");
			if(lossless)
			{
				EnforceJxlEncoder(JxlEncoderSetFrameLossless(settings, JXL_TRUE),
					"Failed to enable JPEG XL lossless mode");
				EnforceJxlEncoder(JxlEncoderSetFrameDistance(settings, 0.0f),
					"Failed to set JPEG XL lossless distance");
			}
			else
			{
				EnforceJxlEncoder(JxlEncoderSetFrameDistance(settings, distance),
					"Failed to set JPEG XL lossy distance");
			}

			const JxlPixelFormat format = PixelFormat(geometry);
			EnforceJxlEncoder(JxlEncoderAddImageFrame(settings, &format, pixels.data(), pixels.size()),
				"Failed to encode JPEG XL image frame");
			JxlEncoderCloseInput(encoder.get());

			std::vector<BYTE> encoded(16384, 0);
			BYTE* next = encoded.data();
			size_t available = encoded.size();
			for(;;)
			{
				const JxlEncoderStatus status =
					JxlEncoderProcessOutput(encoder.get(), reinterpret_cast<uint8_t**>(&next), &available);
				if(status == JXL_ENC_SUCCESS)
					break;
				Enforce(status == JXL_ENC_NEED_MORE_OUTPUT, "Failed to process JPEG XL output");
				const size_t offset = encoded.size() - available;
				encoded.resize(encoded.size() * 2);
				next = encoded.data() + offset;
				available = encoded.size() - offset;
			}
			encoded.resize(encoded.size() - available);
			if(encoded.size() & 1)
				encoded.push_back(0);
			return encoded;
		}

		std::vector<BYTE> DecodeJPEGXL(const std::vector<BYTE>& codestream, const ImageGeometry& geometry)
		{
			Enforce(!codestream.empty(), "JPEG XL codestream is empty");
			const JxlSignature signature = JxlSignatureCheck(codestream.data(), codestream.size());
			Enforce(signature == JXL_SIG_CODESTREAM || signature == JXL_SIG_CONTAINER,
				"Pixel Data is not a JPEG XL codestream or container");

			DecoderPtr decoder(JxlDecoderCreate(0));
			Enforce(decoder.get() != 0, "Failed to create JPEG XL decoder");
			EnforceJxlDecoder(JxlDecoderSubscribeEvents(decoder.get(),
				JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE),
				"Failed to subscribe to JPEG XL decoder events");
			EnforceJxlDecoder(JxlDecoderSetInput(decoder.get(), codestream.data(), codestream.size()),
				"Failed to set JPEG XL decoder input");
			JxlDecoderCloseInput(decoder.get());

			const JxlPixelFormat format = PixelFormat(geometry);
			std::vector<BYTE> pixels;
			bool fullImageDecoded = false;
			for(;;)
			{
				const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
				if(status == JXL_DEC_SUCCESS)
					break;
				if(status == JXL_DEC_BASIC_INFO)
				{
					JxlBasicInfo info;
					EnforceJxlDecoder(JxlDecoderGetBasicInfo(decoder.get(), &info),
						"Failed to read JPEG XL basic information");
					Enforce(info.xsize == geometry.columns, "JPEG XL width does not match DICOM Columns");
					Enforce(info.ysize == geometry.rows, "JPEG XL height does not match DICOM Rows");
					Enforce(info.num_color_channels == geometry.samplesPerPixel,
						"JPEG XL channel count does not match Samples per Pixel");
					Enforce(info.num_extra_channels == 0, "JPEG XL extra channels are not supported");
					Enforce(info.bits_per_sample <= geometry.bitsAllocated,
						"JPEG XL component precision exceeds DICOM Bits Allocated");
				}
				else if(status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
				{
					size_t outputSize = 0;
					EnforceJxlDecoder(JxlDecoderImageOutBufferSize(decoder.get(), &format, &outputSize),
						"Failed to calculate JPEG XL output buffer size");
					Enforce(outputSize == ExpectedNativeSize(geometry),
						"JPEG XL decoded size is inconsistent with DICOM image attributes");
					pixels.assign(outputSize, 0);
					EnforceJxlDecoder(JxlDecoderSetImageOutBuffer(decoder.get(), &format, pixels.data(), pixels.size()),
						"Failed to set JPEG XL output buffer");
					const JxlBitDepth bitDepth = {JXL_BIT_DEPTH_FROM_CODESTREAM, 0, 0};
					EnforceJxlDecoder(JxlDecoderSetImageOutBitDepth(decoder.get(), &bitDepth),
						"Failed to set JPEG XL output bit depth");
				}
				else if(status == JXL_DEC_FULL_IMAGE)
					fullImageDecoded = true;
				else
					throw exception("Failed to decode JPEG XL Pixel Data");
			}
			Enforce(fullImageDecoded, "JPEG XL decoder did not produce a full image");
			return pixels;
		}
#endif
	}

	void DecodeJPEGXLLosslessPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_JPEGXL
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeJPEGXL(codestream, geometry);
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
		throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
	}

	void DecodeJPEGXLPixelData(DataSet& data)
	{
#if DICOMLIB_WITH_JPEGXL
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeJPEGXL(codestream, geometry);
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
		throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
	}

	DataSet EncodeJPEGXLLosslessPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_JPEGXL
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeJPEGXL(pixels, geometry, true, 0.0f);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
	}

	DataSet EncodeJPEGXLPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_JPEGXL
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded =
			EncodeJPEGXL(pixels, geometry, false, static_cast<float>(DICOMLIB_JPEGXL_DISTANCE));
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION, VR_CS, "01");
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_RATIO, VR_DS,
			CompressionRatioString(pixels.size(), encoded.size()));
		PutSingleStringValue(encodedData, TAG_LOSSY_IMAGE_COMPRESSION_METHOD, VR_CS, "ISO_18181_1");
		return encodedData;
#else
		(void)data;
		throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
	}
}
