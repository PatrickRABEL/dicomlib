#include "EncapsulatedUncompressedCodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include <cstdlib>
#include <string>

namespace dicom
{
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
				"Encapsulated Uncompressed Pixel Data requires Rows and Columns");
			Enforce(geometry.samplesPerPixel > 0,
				"Encapsulated Uncompressed Pixel Data requires Samples per Pixel");
			Enforce(geometry.bitsAllocated == 8 || geometry.bitsAllocated == 16,
				"Encapsulated Uncompressed Pixel Data requires 8-bit or 16-bit allocated samples");
			return geometry;
		}

		size_t FrameSize(const ImageGeometry& geometry)
		{
			return size_t(geometry.rows) * size_t(geometry.columns) *
				size_t(geometry.samplesPerPixel) * size_t(geometry.bitsAllocated / 8);
		}

		std::vector<BYTE> NativePixelBytes(const DataSet& data, const ImageGeometry& geometry)
		{
			if(geometry.bitsAllocated == 8)
			{
				const Value& value = data(TAG_PIXEL_DATA);
				Enforce(value.vr() == VR_OB, "8-bit Encapsulated Uncompressed source Pixel Data must be OB");
				return value.Get<TypeFromVR<VR_OB>::Type>();
			}

			const Value& value = data(TAG_PIXEL_DATA);
			Enforce(value.vr() == VR_OW, "16-bit Encapsulated Uncompressed source Pixel Data must be OW");
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
			if(geometry.bitsAllocated == 8)
				data.Put<VR_OB>(TAG_PIXEL_DATA, pixels);
			else
			{
				std::vector<UINT16> words(pixels.size() / 2, 0);
				for(size_t i=0;i<words.size();++i)
					words[i] = UINT16(pixels[i * 2]) | (UINT16(pixels[i * 2 + 1]) << 8);
				data.Put<VR_OW>(TAG_PIXEL_DATA, words);
			}
		}
	}

	void DecodeEncapsulatedUncompressedPixelData(DataSet& data)
	{
		const ImageGeometry geometry = ReadImageGeometry(data);
		const size_t frameSize = FrameSize(geometry);
		std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
		Enforce(fragments.size() == geometry.numberOfFrames,
			"Encapsulated Uncompressed Pixel Data requires one fragment per frame");

		std::vector<BYTE> pixels;
		pixels.reserve(frameSize * geometry.numberOfFrames);
		for(size_t i=0;i<fragments.size();++i)
		{
			Enforce(fragments[i].vr() == VR_OB, "Encapsulated Uncompressed fragments must be OB");
			const std::vector<BYTE>& fragment = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
			const bool hasPadding = (frameSize & 1) != 0;
			Enforce(fragment.size() == frameSize || (hasPadding && fragment.size() == frameSize + 1),
				"Encapsulated Uncompressed fragment size is inconsistent with frame attributes");
			pixels.insert(pixels.end(), fragment.begin(), fragment.begin() + frameSize);
		}
		PutNativePixelData(data, geometry, pixels);
	}

	DataSet EncodeEncapsulatedUncompressedPixelData(const DataSet& data)
	{
		const ImageGeometry geometry = ReadImageGeometry(data);
		const size_t frameSize = FrameSize(geometry);
		const std::vector<BYTE> pixels = NativePixelBytes(data, geometry);
		Enforce(pixels.size() == frameSize * geometry.numberOfFrames,
			"Native Pixel Data size is inconsistent with Encapsulated Uncompressed frame attributes");

		DataSet encodedData = CopyWithoutPixelData(data);
		for(size_t frame=0;frame<geometry.numberOfFrames;++frame)
		{
			const size_t offset = frame * frameSize;
			std::vector<BYTE> fragment(pixels.begin() + offset, pixels.begin() + offset + frameSize);
			if(fragment.size() & 1)
				fragment.push_back(0);
			encodedData.Put<VR_OB>(TAG_PIXEL_DATA, fragment);
		}
		return encodedData;
	}
}
