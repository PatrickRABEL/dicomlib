#include "DeflatedImageFrameCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include "dicomlib/Config.hpp"

#if DICOMLIB_WITH_ZLIB
#include <zlib.h>
#endif

#include <cstdlib>
#include <string>

namespace dicom
{
#if DICOMLIB_WITH_ZLIB
	namespace
	{
		struct ImageGeometry
		{
			UINT16 rows;
			UINT16 columns;
			UINT16 samplesPerPixel;
			UINT16 bitsAllocated;
			size_t numberOfFrames;
		};

		size_t ReadNumberOfFrames(const DataSet& data)
		{
			if(!data.exists(TAG_NUM_FRAMES))
				return 1;
			std::string frames;
			data(TAG_NUM_FRAMES) >> frames;
			const long parsed = std::strtol(frames.c_str(), 0, 10);
			Enforce(parsed > 0, "Number of Frames must be greater than zero");
			return static_cast<size_t>(parsed);
		}

		ImageGeometry ReadImageGeometry(const DataSet& data)
		{
			ImageGeometry geometry = {0, 0, 0, 0, 0};
			data(TAG_ROWS) >> geometry.rows;
			data(TAG_COLUMNS) >> geometry.columns;
			data(TAG_SAMPLES_PER_PX) >> geometry.samplesPerPixel;
			data(TAG_BITS_ALLOC) >> geometry.bitsAllocated;
			geometry.numberOfFrames = ReadNumberOfFrames(data);
			Enforce(geometry.rows > 0 && geometry.columns > 0,
				"Deflated Image Frame Pixel Data requires Rows and Columns");
			Enforce(geometry.samplesPerPixel > 0,
				"Deflated Image Frame Pixel Data requires Samples per Pixel");
			Enforce(geometry.bitsAllocated > 0,
				"Deflated Image Frame Pixel Data requires Bits Allocated");
			return geometry;
		}

		size_t FrameSize(const ImageGeometry& geometry)
		{
			const size_t bits = size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * size_t(geometry.bitsAllocated);
			return (bits + 7) / 8;
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			const Value& value = data(TAG_PIXEL_DATA);
			if(geometry.bitsAllocated <= 8)
			{
				Enforce(value.vr() == VR_OB, "Deflated Image Frame source Pixel Data with Bits Allocated <= 8 must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			Enforce(value.vr() == VR_OW, "Deflated Image Frame source Pixel Data with Bits Allocated > 8 must be OW");
			const std::vector<UINT16>& words = value.Get<TypeFromVR<VR_OW>::Type>();
			std::vector<BYTE> bytes(words.size() * 2, 0);
			for(size_t i=0;i<words.size();++i)
			{
				bytes[i * 2] = BYTE(words[i] & 0xff);
				bytes[i * 2 + 1] = BYTE((words[i] >> 8) & 0xff);
			}
			return bytes;
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

		void PutNativePixelData(DataSet& data, const ImageGeometry& geometry, const std::vector<BYTE>& pixels)
		{
			data.erase(TAG_PIXEL_DATA);
			if(geometry.bitsAllocated <= 8)
				data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
			else
			{
				Enforce((pixels.size() & 1) == 0,
					"Deflated Image Frame decoded Pixel Data with Bits Allocated > 8 must have even byte length");
				std::vector<UINT16> words(pixels.size() / 2, 0);
				for(size_t i=0;i<words.size();++i)
					words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
				data.Put<VR_OW>(TAG_PIXEL_DATA, words);
			}
		}

		std::vector<BYTE> DeflateFrame(const std::vector<BYTE>& frame)
		{
			z_stream stream;
			stream.zalloc = Z_NULL;
			stream.zfree = Z_NULL;
			stream.opaque = Z_NULL;

			if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
				throw exception("Failed to initialize Deflated Image Frame stream");

			stream.next_in = frame.empty() ? Z_NULL : const_cast<Bytef*>(reinterpret_cast<const Bytef*>(frame.data()));
			stream.avail_in = static_cast<uInt>(frame.size());

			std::vector<BYTE> deflated;
			BYTE out[16384];
			int status = Z_OK;
			do
			{
				stream.next_out = reinterpret_cast<Bytef*>(out);
				stream.avail_out = sizeof(out);
				status = deflate(&stream, Z_FINISH);
				if(status != Z_OK && status != Z_STREAM_END)
				{
					deflateEnd(&stream);
					throw exception("Failed to deflate Pixel Data frame");
				}
				deflated.insert(deflated.end(), out, out + (sizeof(out) - stream.avail_out));
			}
			while(status != Z_STREAM_END);

			deflateEnd(&stream);
			if(deflated.size() & 1)
				deflated.push_back(0);
			return deflated;
		}

		std::vector<BYTE> InflateFrame(const std::vector<BYTE>& fragment, size_t frameSize)
		{
			z_stream stream;
			stream.zalloc = Z_NULL;
			stream.zfree = Z_NULL;
			stream.opaque = Z_NULL;

			if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
				throw exception("Failed to initialize Deflated Image Frame inflate stream");

			stream.next_in = fragment.empty() ? Z_NULL : const_cast<Bytef*>(reinterpret_cast<const Bytef*>(fragment.data()));
			stream.avail_in = static_cast<uInt>(fragment.size());

			std::vector<BYTE> inflated;
			inflated.reserve(frameSize);
			BYTE out[16384];
			int status = Z_OK;
			do
			{
				stream.next_out = reinterpret_cast<Bytef*>(out);
				stream.avail_out = sizeof(out);
				status = inflate(&stream, Z_NO_FLUSH);
				if(status != Z_OK && status != Z_STREAM_END)
				{
					inflateEnd(&stream);
					throw exception("Failed to inflate Pixel Data frame");
				}
				inflated.insert(inflated.end(), out, out + (sizeof(out) - stream.avail_out));
			}
			while(status != Z_STREAM_END);

			inflateEnd(&stream);
			Enforce(inflated.size() == frameSize,
				"Deflated Image Frame decoded size is inconsistent with frame attributes");
			return inflated;
		}
	}
#endif

	void DecodeDeflatedImageFramePixelData(DataSet& data)
	{
#if DICOMLIB_WITH_ZLIB
		const ImageGeometry geometry = ReadImageGeometry(data);
		const size_t frameSize = FrameSize(geometry);
		std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
		Enforce(fragments.size() == geometry.numberOfFrames,
			"Deflated Image Frame Pixel Data requires one fragment per frame");

		std::vector<BYTE> pixels;
		pixels.reserve(frameSize * geometry.numberOfFrames);
		for(size_t i=0;i<fragments.size();++i)
		{
			Enforce(fragments[i].vr() == VR_OB, "Deflated Image Frame fragments must be OB");
			const std::vector<BYTE>& fragment = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
			const std::vector<BYTE> frame = InflateFrame(fragment, frameSize);
			pixels.insert(pixels.end(), frame.begin(), frame.end());
		}
		PutNativePixelData(data, geometry, pixels);
#else
		(void)data;
		throw exception("Deflated Image Frame Compression requires DICOMLIB_WITH_ZLIB");
#endif
	}

	DataSet EncodeDeflatedImageFramePixelData(const DataSet& data)
	{
#if DICOMLIB_WITH_ZLIB
		const ImageGeometry geometry = ReadImageGeometry(data);
		const size_t frameSize = FrameSize(geometry);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		Enforce(pixels.size() == frameSize * geometry.numberOfFrames,
			"Native Pixel Data size is inconsistent with Deflated Image Frame attributes");

		DataSet encodedData = CopyWithoutPixelData(data);
		for(size_t frame=0;frame<geometry.numberOfFrames;++frame)
		{
			const size_t offset = frame * frameSize;
			const std::vector<BYTE> nativeFrame(pixels.begin() + offset, pixels.begin() + offset + frameSize);
			encodedData.Put<VR_OB>(TAG_PIXEL_DATA, DeflateFrame(nativeFrame));
		}
		return encodedData;
#else
		(void)data;
		throw exception("Deflated Image Frame Compression requires DICOMLIB_WITH_ZLIB");
#endif
	}
}
