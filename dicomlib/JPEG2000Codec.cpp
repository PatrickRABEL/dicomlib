#include "JPEG2000Codec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_JPEG2000
#include <algorithm>
#include <cstring>
#include <memory>
#include <openjpeg.h>
#include <string>
#endif

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_JPEG2000
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

		struct MemoryReadStream
		{
			const std::vector<BYTE>* data;
			size_t offset;
		};

		struct MemoryWriteStream
		{
			std::vector<BYTE>* data;
			size_t offset;
		};

		struct OpenJPEGMessages
		{
			std::string error;
		};

		OPJ_SIZE_T ReadFromMemory(void* buffer, OPJ_SIZE_T bytes, void* userData)
		{
			MemoryReadStream* stream = static_cast<MemoryReadStream*>(userData);
			if(stream->offset >= stream->data->size())
				return static_cast<OPJ_SIZE_T>(-1);
			const size_t available = stream->data->size() - stream->offset;
			const size_t toRead = std::min<size_t>(available, bytes);
			std::memcpy(buffer, stream->data->data() + stream->offset, toRead);
			stream->offset += toRead;
			return toRead;
		}

		OPJ_SIZE_T WriteToMemory(void* buffer, OPJ_SIZE_T bytes, void* userData)
		{
			MemoryWriteStream* stream = static_cast<MemoryWriteStream*>(userData);
			if(stream->offset + bytes > stream->data->size())
				stream->data->resize(stream->offset + bytes);
			std::memcpy(stream->data->data() + stream->offset, buffer, bytes);
			stream->offset += bytes;
			return bytes;
		}

		OPJ_OFF_T SkipMemory(OPJ_OFF_T bytes, void* userData)
		{
			MemoryReadStream* stream = static_cast<MemoryReadStream*>(userData);
			const OPJ_OFF_T remaining = static_cast<OPJ_OFF_T>(stream->data->size() - stream->offset);
			const OPJ_OFF_T skipped = std::min(bytes, remaining);
			stream->offset += static_cast<size_t>(skipped);
			return skipped;
		}

		OPJ_BOOL SeekReadMemory(OPJ_OFF_T offset, void* userData)
		{
			MemoryReadStream* stream = static_cast<MemoryReadStream*>(userData);
			if(offset < 0 || static_cast<size_t>(offset) > stream->data->size())
				return OPJ_FALSE;
			stream->offset = static_cast<size_t>(offset);
			return OPJ_TRUE;
		}

		OPJ_BOOL SeekWriteMemory(OPJ_OFF_T offset, void* userData)
		{
			MemoryWriteStream* stream = static_cast<MemoryWriteStream*>(userData);
			if(offset < 0)
				return OPJ_FALSE;
			stream->offset = static_cast<size_t>(offset);
			if(stream->offset > stream->data->size())
				stream->data->resize(stream->offset);
			return OPJ_TRUE;
		}

		void IgnoreOpenJPEGMessage(const char*, void*)
		{
		}

		void CaptureOpenJPEGError(const char* message, void* userData)
		{
			OpenJPEGMessages* messages = static_cast<OpenJPEGMessages*>(userData);
			if(message)
				messages->error += message;
		}

		void EnforceOpenJPEG(OPJ_BOOL success, const char* context, const OpenJPEGMessages& messages)
		{
			if(success == OPJ_TRUE)
				return;
			if(!messages.error.empty())
				throw exception(std::string(context) + ": " + messages.error);
			throw exception(context);
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
				"JPEG 2000 support requires 1 or 3 samples per pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"JPEG 2000 support requires 8-bit or 16-bit Pixel Data");
			Enforce(geometry.bitsStored > 0 && geometry.bitsStored <= geometry.bitsAllocated,
				"JPEG 2000 Bits Stored is inconsistent with Bits Allocated");
			Enforce(geometry.pixelRepresentation <= 1, "JPEG 2000 Pixel Representation must be 0 or 1");
			Enforce(geometry.samplesPerPixel == 1 || geometry.planarConfiguration == 0,
				"JPEG 2000 RGB encode/decode support requires Planar Configuration 0");
			return geometry;
		}

		std::vector<BYTE> ConcatenateFragments(const DataSet& data)
		{
			std::vector<BYTE> codestream;
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "JPEG 2000 Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
			{
				Enforce(fragments[i].vr() == VR_OB, "JPEG 2000 fragments must be OB");
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

		opj_stream_t* CreateReadStream(const std::vector<BYTE>& codestream, MemoryReadStream& memory)
		{
			memory.data = &codestream;
			memory.offset = 0;
			opj_stream_t* stream = opj_stream_create(4096, OPJ_TRUE);
			Enforce(stream != 0, "Failed to create JPEG 2000 input stream");
			opj_stream_set_user_data(stream, &memory, 0);
			opj_stream_set_user_data_length(stream, codestream.size());
			opj_stream_set_read_function(stream, ReadFromMemory);
			opj_stream_set_skip_function(stream, SkipMemory);
			opj_stream_set_seek_function(stream, SeekReadMemory);
			return stream;
		}

		opj_stream_t* CreateWriteStream(std::vector<BYTE>& codestream, MemoryWriteStream& memory)
		{
			memory.data = &codestream;
			memory.offset = 0;
			opj_stream_t* stream = opj_stream_create(4096, OPJ_FALSE);
			Enforce(stream != 0, "Failed to create JPEG 2000 output stream");
			opj_stream_set_user_data(stream, &memory, 0);
			opj_stream_set_write_function(stream, WriteToMemory);
			opj_stream_set_seek_function(stream, SeekWriteMemory);
			return stream;
		}

		void DestroyImage(opj_image_t* image)
		{
			if(image)
				opj_image_destroy(image);
		}

		void DestroyCodec(opj_codec_t* codec)
		{
			if(codec)
				opj_destroy_codec(codec);
		}

		void DestroyStream(opj_stream_t* stream)
		{
			if(stream)
				opj_stream_destroy(stream);
		}

		struct ImageDeleter
		{
			void operator()(opj_image_t* image) const
			{
				DestroyImage(image);
			}
		};

		struct CodecDeleter
		{
			void operator()(opj_codec_t* codec) const
			{
				DestroyCodec(codec);
			}
		};

		struct StreamDeleter
		{
			void operator()(opj_stream_t* stream) const
			{
				DestroyStream(stream);
			}
		};

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

		int ReadNativeSample(const std::vector<BYTE>& bytes, size_t offset, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const BYTE value = bytes[offset];
				if(geometry.pixelRepresentation == 0)
					return value;
				return static_cast<int>(static_cast<signed char>(value));
			}

			const UINT16 value = UINT16(bytes[offset]) | (UINT16(bytes[offset + 1]) << 8);
			if(geometry.pixelRepresentation == 0)
				return value;
			return static_cast<int>(static_cast<short>(value));
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const Value& value = data(TAG_PIXEL_DATA);
				Enforce(value.vr() == VR_OB, "8-bit JPEG 2000 Pixel Data must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OW, "16-bit JPEG 2000 Pixel Data must be OW");
			const std::vector<UINT16>& words = value.Get<TypeFromVR<VR_OW>::Type>();
			std::vector<BYTE> bytes(words.size() * 2, 0);
			for(size_t i=0;i<words.size();++i)
			{
				bytes[i * 2] = BYTE(words[i] & 0xff);
				bytes[i * 2 + 1] = BYTE((words[i] >> 8) & 0xff);
			}
			return bytes;
		}

		opj_image_t* CreateImageFromNativePixels(const std::vector<BYTE>& pixels, const ImageGeometry& geometry)
		{
			const size_t bytesPerSample = geometry.bitsAllocated / 8;
			const size_t sampleCount = size_t(geometry.rows) * size_t(geometry.columns) * size_t(geometry.samplesPerPixel);
			Enforce(pixels.size() == sampleCount * bytesPerSample,
				"Native Pixel Data size is inconsistent with JPEG 2000 image attributes");

			opj_image_cmptparm_t componentParameters[3];
			std::memset(componentParameters, 0, sizeof(componentParameters));
			for(UINT16 c=0;c<geometry.samplesPerPixel;++c)
			{
				componentParameters[c].dx = 1;
				componentParameters[c].dy = 1;
				componentParameters[c].w = geometry.columns;
				componentParameters[c].h = geometry.rows;
				componentParameters[c].prec = geometry.bitsStored;
				componentParameters[c].bpp = geometry.bitsStored;
				componentParameters[c].sgnd = geometry.pixelRepresentation;
			}

			opj_image_t* image = opj_image_create(
				geometry.samplesPerPixel,
				componentParameters,
				geometry.samplesPerPixel == 1 ? OPJ_CLRSPC_GRAY : OPJ_CLRSPC_SRGB);
			Enforce(image != 0, "Failed to create JPEG 2000 image");
			image->x0 = 0;
			image->y0 = 0;
			image->x1 = geometry.columns;
			image->y1 = geometry.rows;

			for(UINT16 c=0;c<geometry.samplesPerPixel;++c)
			{
				opj_image_comp_t& component = image->comps[c];
				for(UINT16 y=0;y<geometry.rows;++y)
				{
					for(UINT16 x=0;x<geometry.columns;++x)
					{
						const size_t nativeSample = (size_t(y) * size_t(geometry.columns) * size_t(geometry.samplesPerPixel)) +
							(size_t(x) * size_t(geometry.samplesPerPixel)) + c;
						component.data[size_t(y) * size_t(geometry.columns) + x] =
							ReadNativeSample(pixels, nativeSample * bytesPerSample, geometry);
					}
				}
			}

			return image;
		}

		std::vector<BYTE> EncodeJPEG2000(const std::vector<BYTE>& pixels, const ImageGeometry& geometry)
		{
			std::unique_ptr<opj_image_t, ImageDeleter> image(CreateImageFromNativePixels(pixels, geometry));
			std::unique_ptr<opj_codec_t, CodecDeleter> codec(opj_create_compress(OPJ_CODEC_J2K));
			Enforce(codec != 0, "Failed to create JPEG 2000 encoder");
			OpenJPEGMessages messages;
			opj_set_info_handler(codec.get(), IgnoreOpenJPEGMessage, 0);
			opj_set_warning_handler(codec.get(), IgnoreOpenJPEGMessage, 0);
			opj_set_error_handler(codec.get(), CaptureOpenJPEGError, &messages);

			opj_cparameters_t parameters;
			opj_set_default_encoder_parameters(&parameters);
			parameters.tcp_numlayers = 1;
			parameters.tcp_rates[0] = 0;
			parameters.irreversible = 0;
			parameters.numresolution = ResolutionCount(geometry);
			parameters.tcp_mct = geometry.samplesPerPixel == 3 ? 1 : 0;
			parameters.cod_format = 0;
			EnforceOpenJPEG(opj_setup_encoder(codec.get(), &parameters, image.get()),
				"Failed to setup JPEG 2000 encoder", messages);

			std::vector<BYTE> codestream;
			MemoryWriteStream memory;
			std::unique_ptr<opj_stream_t, StreamDeleter> stream(CreateWriteStream(codestream, memory));
			EnforceOpenJPEG(opj_start_compress(codec.get(), image.get(), stream.get()),
				"Failed to start JPEG 2000 compression", messages);
			EnforceOpenJPEG(opj_encode(codec.get(), stream.get()),
				"Failed to encode JPEG 2000 Pixel Data", messages);
			EnforceOpenJPEG(opj_end_compress(codec.get(), stream.get()),
				"Failed to finish JPEG 2000 compression", messages);
			if(codestream.size() & 1)
				codestream.push_back(0);
			return codestream;
		}

		std::vector<BYTE> DecodeJPEG2000(const std::vector<BYTE>& codestream, const ImageGeometry& geometry)
		{
			Enforce(!codestream.empty(), "JPEG 2000 codestream is empty");
			MemoryReadStream memory;
			std::unique_ptr<opj_stream_t, StreamDeleter> stream(CreateReadStream(codestream, memory));
			std::unique_ptr<opj_codec_t, CodecDeleter> codec(opj_create_decompress(OPJ_CODEC_J2K));
			Enforce(codec != 0, "Failed to create JPEG 2000 decoder");
			OpenJPEGMessages messages;
			opj_set_info_handler(codec.get(), IgnoreOpenJPEGMessage, 0);
			opj_set_warning_handler(codec.get(), IgnoreOpenJPEGMessage, 0);
			opj_set_error_handler(codec.get(), CaptureOpenJPEGError, &messages);

			opj_dparameters_t parameters;
			opj_set_default_decoder_parameters(&parameters);
			EnforceOpenJPEG(opj_setup_decoder(codec.get(), &parameters),
				"Failed to setup JPEG 2000 decoder", messages);

			opj_image_t* rawImage = 0;
			const OPJ_BOOL headerRead = opj_read_header(stream.get(), codec.get(), &rawImage);
			std::unique_ptr<opj_image_t, ImageDeleter> decodedImage(rawImage);
			EnforceOpenJPEG(headerRead,
				"Failed to read JPEG 2000 header", messages);
			EnforceOpenJPEG(opj_decode(codec.get(), stream.get(), decodedImage.get()),
				"Failed to decode JPEG 2000 Pixel Data", messages);
			EnforceOpenJPEG(opj_end_decompress(codec.get(), stream.get()),
				"Failed to finish JPEG 2000 decompression", messages);
			Enforce(decodedImage->x1 - decodedImage->x0 == geometry.columns, "JPEG 2000 width does not match DICOM Columns");
			Enforce(decodedImage->y1 - decodedImage->y0 == geometry.rows, "JPEG 2000 height does not match DICOM Rows");
			Enforce(decodedImage->numcomps == geometry.samplesPerPixel, "JPEG 2000 component count does not match Samples per Pixel");

			const size_t bytesPerSample = geometry.bitsAllocated / 8;
			std::vector<BYTE> pixels(size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * bytesPerSample, 0);
			for(UINT16 c=0;c<geometry.samplesPerPixel;++c)
			{
				const opj_image_comp_t& component = decodedImage->comps[c];
				Enforce(component.prec <= geometry.bitsAllocated,
					"JPEG 2000 component precision exceeds DICOM Bits Allocated");
				Enforce(component.sgnd == geometry.pixelRepresentation,
					"JPEG 2000 component signedness does not match Pixel Representation");
				for(UINT16 y=0;y<geometry.rows;++y)
				{
					for(UINT16 x=0;x<geometry.columns;++x)
					{
						const int sample = component.data[size_t(y) * size_t(geometry.columns) + x];
						const size_t nativeSample = (size_t(y) * size_t(geometry.columns) * size_t(geometry.samplesPerPixel)) +
							(size_t(x) * size_t(geometry.samplesPerPixel)) + c;
						const size_t offset = nativeSample * bytesPerSample;
						if(geometry.bitsAllocated == 8)
							pixels[offset] = BYTE(sample & 0xff);
						else
						{
							const UINT16 word = UINT16(sample);
							pixels[offset] = BYTE(word & 0xff);
							pixels[offset + 1] = BYTE((word >> 8) & 0xff);
						}
					}
				}
			}

			return pixels;
		}
#endif
	}

	void DecodeJPEG2000PixelData(DataSet& data)
	{
#if DICOMLIB_WITH_JPEG2000
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> codestream = ConcatenateFragments(data);
		const std::vector<BYTE> pixels = DecodeJPEG2000(codestream, geometry);
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
		throw exception("JPEG 2000 requires DICOMLIB_WITH_JPEG2000");
#endif
	}

	DataSet EncodeJPEG2000LosslessPixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_JPEG2000
		const ImageGeometry geometry = ReadImageGeometry(data);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		DataSet encodedData = CopyWithoutPixelData(data);
		const std::vector<BYTE> encoded = EncodeJPEG2000(pixels, geometry);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encoded);
		return encodedData;
#else
		(void)data;
		throw exception("JPEG 2000 requires DICOMLIB_WITH_JPEG2000");
#endif
	}
}
