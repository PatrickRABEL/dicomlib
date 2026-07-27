#include "RLECodec.hpp"

#include "Exceptions.hpp"
#include "Tag.hpp"
#include "VR.hpp"

#include <algorithm>
#include <cstdint>

namespace dicom
{
	namespace
	{
		UINT32 ReadUL(const std::vector<BYTE>& data, size_t offset)
		{
			Enforce(offset + 4 <= data.size(), "RLE header is truncated");
			return UINT32(data[offset]) |
				(UINT32(data[offset + 1]) << 8) |
				(UINT32(data[offset + 2]) << 16) |
				(UINT32(data[offset + 3]) << 24);
		}

		void WriteUL(std::vector<BYTE>& data, size_t offset, UINT32 value)
		{
			Enforce(offset + 4 <= data.size(), "RLE header write out of range");
			data[offset] = BYTE(value & 0xff);
			data[offset + 1] = BYTE((value >> 8) & 0xff);
			data[offset + 2] = BYTE((value >> 16) & 0xff);
			data[offset + 3] = BYTE((value >> 24) & 0xff);
		}

		std::vector<BYTE> DecodeSegment(
			const std::vector<BYTE>& encoded,
			size_t begin,
			size_t end,
			size_t expectedSize)
		{
			std::vector<BYTE> decoded;
			decoded.reserve(expectedSize);

			size_t i = begin;
			while(decoded.size() < expectedSize)
			{
				Enforce(i < end, "RLE segment ended before expected output size");
				const int n = static_cast<std::int8_t>(encoded[i++]);
				if(n >= 0)
				{
					const size_t count = static_cast<size_t>(n) + 1;
					Enforce(i + count <= end, "RLE literal run exceeds segment length");
					Enforce(decoded.size() + count <= expectedSize, "RLE literal run exceeds expected output size");
					decoded.insert(decoded.end(), encoded.begin() + i, encoded.begin() + i + count);
					i += count;
				}
				else if(n >= -127)
				{
					Enforce(i < end, "RLE replicate run has no byte value");
					const BYTE value = encoded[i++];
					const size_t count = static_cast<size_t>(-n) + 1;
					Enforce(decoded.size() + count <= expectedSize, "RLE replicate run exceeds expected output size");
					decoded.insert(decoded.end(), count, value);
				}
			}

			if(i < end)
			{
				Enforce(end - i == 1 && encoded[i] == 0, "RLE segment contains trailing non-padding bytes");
			}
			return decoded;
		}

		void EncodeLiteralRows(
			const std::vector<BYTE>& segment,
			size_t rowLength,
			std::vector<BYTE>& encoded)
		{
			for(size_t row = 0; row < segment.size(); row += rowLength)
			{
				size_t remaining = rowLength;
				size_t offset = row;
				while(remaining > 0)
				{
					const size_t count = std::min<size_t>(remaining, 128);
					encoded.push_back(BYTE(count - 1));
					encoded.insert(encoded.end(), segment.begin() + offset, segment.begin() + offset + count);
					offset += count;
					remaining -= count;
				}
			}
			if(encoded.size() & 1)
				encoded.push_back(0);
		}

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

		std::vector<BYTE> NativePixelBytes(const DataSet& data)
		{
			const Value& value = data(TAG_PIXEL_DATA);
			if(value.vr() == VR_OB)
				return value.Get<TypeFromVR<VR_OB>::Type>();

			if(value.vr() == VR_OW)
			{
				const TypeFromVR<VR_OW>::Type& words = value.Get<TypeFromVR<VR_OW>::Type>();
				std::vector<BYTE> bytes;
				bytes.reserve(words.size() * 2);
				for(size_t i=0;i<words.size();++i)
				{
					bytes.push_back(BYTE(words[i] & 0xff));
					bytes.push_back(BYTE((words[i] >> 8) & 0xff));
				}
				return bytes;
			}

			throw exception("RLE Pixel Data must be OB or OW");
		}

		void PutNativePixelBytes(DataSet& data, const std::vector<BYTE>& bytes, UINT16 bitsAllocated)
		{
			data.erase(TAG_PIXEL_DATA);
			if(bitsAllocated <= 8)
			{
				data.Put<VR_OB>(TAG_PIXEL_DATA, bytes);
				return;
			}

			Enforce((bytes.size() & 1) == 0, "Decoded RLE word Pixel Data has odd byte length");
			TypeFromVR<VR_OW>::Type words(bytes.size() / 2, 0);
			for(size_t i=0;i<words.size();++i)
				words[i] = UINT16(bytes[i * 2]) | (UINT16(bytes[i * 2 + 1]) << 8);
			data.Put<VR_OW>(TAG_PIXEL_DATA, words);
		}
	}

	std::vector<BYTE> DecodeRLELosslessFrame(
		const std::vector<BYTE>& encodedFrame,
		UINT16 rows,
		UINT16 columns,
		UINT16 samplesPerPixel,
		UINT16 bitsAllocated)
	{
		Enforce(encodedFrame.size() >= 64, "RLE frame is shorter than the fixed header");
		Enforce(bitsAllocated != 0 && (bitsAllocated % 8) == 0, "RLE Bits Allocated must be byte aligned");

		const size_t bytesPerSample = bitsAllocated / 8;
		const size_t framePixels = size_t(rows) * size_t(columns);
		const size_t expectedSegments = size_t(samplesPerPixel) * bytesPerSample;
		Enforce(expectedSegments > 0 && expectedSegments <= 15, "RLE segment count is outside DICOM limits");

		const UINT32 segmentCount = ReadUL(encodedFrame, 0);
		Enforce(segmentCount == expectedSegments, "RLE segment count is inconsistent with image attributes");

		std::vector<UINT32> offsets(segmentCount, 0);
		for(size_t i=0;i<segmentCount;++i)
		{
			offsets[i] = ReadUL(encodedFrame, 4 * (i + 1));
			Enforce(offsets[i] >= 64 && offsets[i] <= encodedFrame.size(), "RLE segment offset is invalid");
			if(i > 0)
				Enforce(offsets[i] >= offsets[i - 1], "RLE segment offsets must be ordered");
		}

		std::vector<BYTE> native(framePixels * size_t(samplesPerPixel) * bytesPerSample, 0);
		for(size_t segmentIndex=0;segmentIndex<segmentCount;++segmentIndex)
		{
			const size_t begin = offsets[segmentIndex];
			const size_t end = (segmentIndex + 1 < segmentCount) ? offsets[segmentIndex + 1] : encodedFrame.size();
			const std::vector<BYTE> decoded = DecodeSegment(encodedFrame, begin, end, framePixels);

			const size_t sample = segmentIndex / bytesPerSample;
			const size_t byteFromMostSignificant = segmentIndex % bytesPerSample;
			const size_t littleEndianByte = bytesPerSample - 1 - byteFromMostSignificant;
			for(size_t pixel=0;pixel<framePixels;++pixel)
			{
				const size_t nativeOffset = ((pixel * size_t(samplesPerPixel) + sample) * bytesPerSample) + littleEndianByte;
				native[nativeOffset] = decoded[pixel];
			}
		}
		return native;
	}

	std::vector<BYTE> EncodeRLELosslessFrame(
		const std::vector<BYTE>& nativeFrame,
		UINT16 rows,
		UINT16 columns,
		UINT16 samplesPerPixel,
		UINT16 bitsAllocated)
	{
		Enforce(bitsAllocated != 0 && (bitsAllocated % 8) == 0, "RLE Bits Allocated must be byte aligned");

		const size_t bytesPerSample = bitsAllocated / 8;
		const size_t framePixels = size_t(rows) * size_t(columns);
		const size_t expectedSegments = size_t(samplesPerPixel) * bytesPerSample;
		Enforce(expectedSegments > 0 && expectedSegments <= 15, "RLE segment count is outside DICOM limits");
		Enforce(nativeFrame.size() == framePixels * size_t(samplesPerPixel) * bytesPerSample,
			"Native Pixel Data size is inconsistent with RLE image attributes");

		std::vector<std::vector<BYTE> > encodedSegments(expectedSegments);
		for(size_t segmentIndex=0;segmentIndex<expectedSegments;++segmentIndex)
		{
			const size_t sample = segmentIndex / bytesPerSample;
			const size_t byteFromMostSignificant = segmentIndex % bytesPerSample;
			const size_t littleEndianByte = bytesPerSample - 1 - byteFromMostSignificant;

			std::vector<BYTE> segment(framePixels, 0);
			for(size_t pixel=0;pixel<framePixels;++pixel)
			{
				const size_t nativeOffset = ((pixel * size_t(samplesPerPixel) + sample) * bytesPerSample) + littleEndianByte;
				segment[pixel] = nativeFrame[nativeOffset];
			}
			EncodeLiteralRows(segment, columns, encodedSegments[segmentIndex]);
		}

		std::vector<BYTE> encoded(64, 0);
		WriteUL(encoded, 0, static_cast<UINT32>(expectedSegments));
		size_t offset = 64;
		for(size_t i=0;i<expectedSegments;++i)
		{
			WriteUL(encoded, 4 * (i + 1), static_cast<UINT32>(offset));
			encoded.insert(encoded.end(), encodedSegments[i].begin(), encodedSegments[i].end());
			offset += encodedSegments[i].size();
		}
		return encoded;
	}

	void DecodeRLELosslessPixelData(DataSet& data)
	{
		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		ReadImageGeometry(data, rows, columns, samplesPerPixel, bitsAllocated);

		std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
		Enforce(!fragments.empty(), "RLE Pixel Data has no fragments");

		std::vector<BYTE> native;
		for(size_t i=0;i<fragments.size();++i)
		{
			Enforce(fragments[i].vr() == VR_OB, "RLE fragments must be OB");
			const std::vector<BYTE>& fragment = fragments[i].Get<TypeFromVR<VR_OB>::Type>();
			const std::vector<BYTE> frame = DecodeRLELosslessFrame(fragment, rows, columns, samplesPerPixel, bitsAllocated);
			native.insert(native.end(), frame.begin(), frame.end());
		}

		PutNativePixelBytes(data, native, bitsAllocated);
	}

	DataSet EncodeRLELosslessPixelData(const DataSet& data)
	{
		UINT16 rows = 0;
		UINT16 columns = 0;
		UINT16 samplesPerPixel = 0;
		UINT16 bitsAllocated = 0;
		ReadImageGeometry(data, rows, columns, samplesPerPixel, bitsAllocated);

		DataSet encodedData;
		for(DataSet::const_iterator i=data.begin();i!=data.end();++i)
		{
			if(i->first != TAG_PIXEL_DATA)
				encodedData.insert(*i);
		}

		const std::vector<BYTE> native = NativePixelBytes(data);
		const std::vector<BYTE> encodedFrame = EncodeRLELosslessFrame(
			native,
			rows,
			columns,
			samplesPerPixel,
			bitsAllocated);
		encodedData.Put<VR_OB>(TAG_PIXEL_DATA, encodedFrame);
		return encodedData;
	}
}
