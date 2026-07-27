/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/
#include <iostream>
#include <type_traits>
#include "EncapsulatedUncompressedCodec.hpp"
#include "Encoder.hpp"
#include "Exceptions.hpp"
#include "HTJ2KCodec.hpp"
#include "JPEG2000Codec.hpp"
#include "JPEGCodec.hpp"
#include "JPEGLSCodec.hpp"
#include "JPEGXLCodec.hpp"
#include "RLECodec.hpp"
#include "UIDs.hpp"
#include "dicomlib/Config.hpp"
#include "iso646.h"
#if DICOMLIB_WITH_ZLIB
#include <zlib.h>
#endif
using namespace std;

namespace dicom
{
	namespace
	{
#if DICOMLIB_WITH_ZLIB
		void DeflateBuffer(const Buffer& input, Buffer& output)
		{
			z_stream stream;
			stream.zalloc = Z_NULL;
			stream.zfree = Z_NULL;
			stream.opaque = Z_NULL;

			if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
				throw exception("Failed to initialize deflate stream");

			stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
			stream.avail_in = static_cast<uInt>(input.size());

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
					throw exception("Failed to deflate DICOM data set");
				}
				output.insert(output.end(), out, out + (sizeof(out) - stream.avail_out));
			}
			while(status != Z_STREAM_END);

			deflateEnd(&stream);
		}
#endif
	}

	class Encoder
	{
	public:
		Encoder(Buffer& buffer,const DataSet& ds,TS ts):buffer_(buffer),dataset_(ds),ts_(ts){}
		UINT32 Encode();
	private:
		Buffer& buffer_;
		const DataSet& dataset_;
		TS ts_;

		UINT32 EncodeElement(const DataSet::value_type& element)const;
		UINT32 WriteLengthAndVR(UINT32 length,VR vr);
		UINT32 SendRange(DataSet::const_iterator Begin,DataSet::const_iterator End);
		UINT32 SendSequenceItemInExplicitLength(Buffer& B,const DataSet& SqItem);
		UINT32 SendSequence(const Sequence& sequence,bool explicit_length = true);

		template<VR vr>
		UINT32 SendFundamentalType(DataSet::const_iterator Begin,DataSet::const_iterator End)
		{
			UINT32 sentlength=0;
			typedef typename TypeFromVR<vr>::Type Type;
			static_assert(std::is_fundamental<Type>::value, "SendFundamentalType requires a fundamental type");
			std::vector<Value> vv;
			for(;Begin!=End;Begin++)
			{
				if(Begin->second.empty())
					continue;
				vv.push_back(Begin->second);
			}

			UINT32 Length = (UINT32)vv.size();//Should be identical to End-Begin, but we don't have subtraction operator available.
			sentlength += WriteLengthAndVR(sizeof(Type)*Length,vr);
			for(size_t i=0;i<vv.size();i++)
			{
				Type data;
				vv[i] >> data;
				buffer_ << data;
				sentlength += sizeof(Type);
			}
			return sentlength;
		}

		UINT32 SendAttributeTag(DataSet::const_iterator Begin,DataSet::const_iterator End)
		{
			UINT32 sentlength=0;
			UINT32 Length = (UINT32)dataset_.count(Begin->first);//Should be identical to End-Begin, but we don't have subtraction operator available.

			sentlength += WriteLengthAndVR(sizeof(Tag)*Length,VR_AT);
			for(;Begin!=End;Begin++)
			{
				Tag data;
				Begin->second >> data;
				buffer_ << data;
				sentlength += sizeof(Tag);
			}
			return sentlength;
		}


		template <VR vr>
		UINT32 SendString(DataSet::const_iterator Begin,DataSet::const_iterator End)
		{
			UINT32 sentlength=0;
			StaticVRCheck<std::string,vr>();
			Tag tag=Begin->first;
			//delimiter character is '\'
			std::string StringToSend;
			for(;Begin!=End;Begin++)
			{
				std::string s;
				const Value& value=Begin->second;
				if((vr!=value.vr()) or(tag!=Begin->first))// this block is basically an ASSERT, ie I expect it to never be entered.
					//we have a major problem.
					throw dicom::exception("Some inconsistency in dataset.");
				value >> s;
				StringToSend.append(s);
				StringToSend.append(1,'\\');//delimiter
			}
			StringToSend.erase(StringToSend.end()-1);//remove last delimiter.
			if(StringToSend.size() bitand 0x01)//length is odd
				StringToSend.append(1,' ');//string length must be even.

			sentlength += WriteLengthAndVR((UINT32)StringToSend.size(),vr);
			buffer_<<StringToSend;
			sentlength += StringToSend.length();
			return sentlength;
		}
		UINT32 SendUID(DataSet::const_iterator Begin, DataSet::const_iterator End)
		{
			UINT32 sentlength=0;
			Tag tag=Begin->first;
			std::string StringToSend;
			for(;Begin!=End;Begin++)
			{
				UID uid;
				const Value& value=Begin->second;
				if((VR_UI!=value.vr()) or(tag!=Begin->first))
					throw dicom::exception("Some inconsistency in dataset.");
				value >> uid;
				StringToSend.append(uid.str());
				StringToSend.append(1,'\\');//delimiter
			}
			StringToSend.erase(StringToSend.end()-1);//remove last delimiter.
			if(StringToSend.size() bitand 0x01)	//length is odd
				StringToSend.append(1,'\0');	//length must be even, NULL character is used for padding UIDs

			sentlength += WriteLengthAndVR((UINT32)StringToSend.size(),VR_UI);
			buffer_<<StringToSend;
			sentlength += StringToSend.length();
			return sentlength;
		}


		UINT32 SendOB(DataSet::const_iterator Begin, DataSet::const_iterator End)
		{	
			UINT32 sentlength=0;
			typedef TypeFromVR<VR_OB>::Type Type;

			Tag tag = Begin->first;
			int fragments=dataset_.count(tag);

			Enforce(ts_.isEncapsulated() || (1==fragments),"Only encoded data can have multiple image fragments.");

			if(!ts_.isEncapsulated())//just send the data
			{
				const Type& ByteVector = Begin->second.Get<Type>();
				sentlength += WriteLengthAndVR((UINT32)ByteVector.size(),VR_OB);
				buffer_.AddVector(ByteVector);
				sentlength += ByteVector.size();
			}
			else	//send the data as a series of fragments as defined in Part5 Annex 4
			{
				sentlength += WriteLengthAndVR(UNDEFINED_LENGTH,VR_OB);

				//send empty offset table - not supported.

				buffer_ << TAG_ITEM;
				sentlength += sizeof(Tag);
				buffer_ << UINT32(0x00);//no offset table.
				sentlength += sizeof(UINT32);

				for(;Begin!=End;Begin++)
				{
					buffer_ << TAG_ITEM;
					sentlength += sizeof(Tag);
					const Type& ByteVector = Begin->second.Get<Type>();
					buffer_ << UINT32(ByteVector.size());
					sentlength += sizeof(UINT32);
					buffer_.AddVector(ByteVector);
					sentlength += ByteVector.size();
				}

				buffer_ << TAG_SEQ_DELIM_ITEM;
				sentlength += sizeof(Tag);
				buffer_ << UINT32(0x00);			
				sentlength += sizeof(UINT32);
			}
			return sentlength;
		}

		template <VR vr>
		UINT32 SendVector(DataSet::const_iterator Begin, DataSet::const_iterator End)
		{
			(void)End;
			UINT32 sentlength=0;
			typedef typename TypeFromVR<vr>::Type VectorType;
			typedef typename VectorType::value_type ItemType;
			const VectorType& Vector = Begin->second.Get<VectorType>();

			UINT32 ByteLength=(UINT32)(Vector.size()*sizeof(ItemType));
			sentlength += WriteLengthAndVR(ByteLength,vr);

			for(size_t i=0;i<Vector.size();++i)
				buffer_ << Vector[i];
			sentlength += ByteLength;
			return sentlength;
		}
	};

	UINT32 Encoder::SendRange(DataSet::const_iterator Begin,DataSet::const_iterator End)
	{
	//we might want an ASSERT here to check that the range truly is consistent,
	//i.e. only consists of elements sharing the same Tag and VR...
		UINT32 sentlength=0;
		Tag tag = Begin->first;
		VR vr = Begin->second.vr();

		buffer_ << tag;
		sentlength += sizeof(Tag);

		switch(vr)
		{
		case VR_AE:
			sentlength += SendString<VR_AE>(Begin,End);
			break;
		case VR_AS:
			sentlength += SendString<VR_AS>(Begin,End);
			break;
		case VR_AT:
			sentlength += SendAttributeTag(Begin,End);
			break;
		case VR_CS:
			sentlength += SendString<VR_CS>(Begin,End);
			break;
		case VR_DA:
			sentlength += SendString<VR_DA>(Begin,End);
			break;
		case VR_DS:
			sentlength += SendString<VR_DS>(Begin,End);
			break;
		case VR_DT:
			sentlength += SendString<VR_DT>(Begin,End);
			break;
		case VR_FD:
			sentlength += SendFundamentalType <VR_FD>(Begin,End);
			break;
		case VR_FL:
			sentlength += SendFundamentalType<VR_FL>(Begin,End);
			break;
		case VR_IS:
			sentlength += SendString<VR_IS>(Begin,End);
			break;
		case VR_LO:
			sentlength += SendString<VR_LO>(Begin,End);
			break;
		case VR_LT:
			sentlength += SendString<VR_LT>(Begin,End);
			break;
		case VR_OB:
			sentlength += SendOB(Begin,End);
			break;
		case VR_OD:
			sentlength += SendVector<VR_OD>(Begin,End);
			break;
		case VR_OF:
			sentlength += SendVector<VR_OF>(Begin,End);
			break;
		case VR_OL:
			sentlength += SendVector<VR_OL>(Begin,End);
			break;
		case VR_OW:
			{
				typedef TypeFromVR<VR_OW>::Type Type;
				const Type& WordVector = Begin->second.Get<Type>();

				UINT32 ByteLength=WordVector.size()*2;
				sentlength += WriteLengthAndVR(ByteLength,VR_OW);

				buffer_.AddVector(WordVector);
				sentlength += ByteLength;
			break;
			}
		case VR_OV:
			sentlength += SendVector<VR_OV>(Begin,End);
			break;
		case VR_PN:
			sentlength +=  SendString<VR_PN>(Begin,End);
			break;
		case VR_SH:
			sentlength +=  SendString<VR_SH>(Begin,End);
			break;
		case VR_SL:
			sentlength +=  SendFundamentalType<VR_SL>(Begin,End);
			break;
		case VR_SQ:
		{
			const Sequence& sequence=Begin->second.Get<Sequence>();
			sentlength += SendSequence(sequence);
			break;
		}
		case VR_SS:
			sentlength +=  SendFundamentalType<VR_SS>(Begin,End);
			break;
		case VR_ST:
			sentlength +=  SendString<VR_ST>(Begin,End);
			break;
		case VR_SV:
			sentlength +=  SendFundamentalType<VR_SV>(Begin,End);
			break;
		case VR_TM:
			sentlength +=  SendString<VR_TM>(Begin,End);
			break;
		case VR_UC:
			sentlength +=  SendString<VR_UC>(Begin,End);
			break;
		case VR_UI:
			sentlength +=  SendUID(Begin,End);
			break;
		case VR_UL:
			sentlength +=  SendFundamentalType<VR_UL>(Begin,End);
			break;
		case VR_UN:
			{
				typedef TypeFromVR<VR_UN>::Type Type;
				const Type& ByteVector = Begin->second.Get<Type>();
				sentlength += WriteLengthAndVR((UINT32)ByteVector.size(),VR_UN);
				buffer_.AddVector(ByteVector);
				sentlength += ByteVector.size();
			break;
			}
		case VR_UR:
			sentlength += SendString<VR_UR>(Begin,End);
			break;
		case VR_US:
			sentlength += SendFundamentalType<VR_US>(Begin,End);
			break;
		case VR_UT:
			sentlength += SendString<VR_UT>(Begin,End);
			break;
		case VR_UV:
			sentlength += SendFundamentalType<VR_UV>(Begin,End);
			break;
		default:
			cout << "Unknown VR: " << vr  << " in EncodeElement()" << endl;
			throw BadVR(vr);
		}
		return sentlength;
	}

	UINT32 Encoder::WriteLengthAndVR(UINT32 length,VR vr)
	{
		UINT32 sentlength=0;
		if(ts_.isExplicitVR())
		{
			buffer_ << BYTE(vr);//byte 1 -Sam
			buffer_ << BYTE(vr>>8);//byte 2 -Sam
			sentlength +=2;
			//buffer_ << UINT16(vr);
			if( VR_UN == vr || VR_SQ == vr || VR_OW == vr || VR_OB == vr || VR_OD == vr || VR_OF == vr || VR_OL == vr || VR_OV == vr || VR_UC == vr || VR_UR == vr || VR_UT == vr)
			{

				buffer_ << UINT16(0);	//reserved
				sentlength += sizeof(UINT16);
				buffer_ << length;		//4 bytes
				sentlength += sizeof(UINT32);
			}
			else
			{
				buffer_<<UINT16(length);//2 bytes
				sentlength += sizeof(UINT16);
			}
		}
		else
		{
			//no VR info sent
			buffer_ << length;			//4 bytes
			sentlength += sizeof(UINT32);
		}
		return sentlength;
	}

	UINT32 Encoder::Encode()
	{
		UINT32 sentlength=0;
		DataSet::const_iterator I = dataset_.begin();

		while(I!=dataset_.end())
		{
			Tag tag=I->first;
			pair<DataSet::const_iterator,DataSet::const_iterator> p=dataset_.equal_range(tag);
   			sentlength += SendRange(p.first,p.second);
			I=p.second;
		}
		return sentlength;
	}

	/*!

		This should be simpler than Decoder::DecodeSequence(), because we only need
		to implement ONE way of sending multiple data sets.
		Which shall we use?  Exlicit length, or Sequence Delimitation Items?  I
		think the second is the simplest... -Trevor

		When I deal with programs using OFFIS dcmtk (OFFIS_DCMTK_354), I have a problem 
		that a program with OFFIS can not interpret the undefined length correctly. I try 
		to implement explicit length like tables in 7.5-1,2,3 in DICOM Part 5. I have
		to add one more level of send, the SequenceItem. One difficulty of implementing
		explicit length is that we do not know the length of the sequence until we finish
		sending it, but DICOM like us to send the length before the items. I will have to 
		send the sequence item to another buffer and calculate its length. Then copy the
		new buffer to the original buffer. -Sam Shen
	*/

	UINT32 Encoder::SendSequenceItemInExplicitLength(Buffer& B,const DataSet& SqItem)
	{
		UINT32 sentlength=0;
		int ByteOrder=ts_.isBigEndian()?__BIG_ENDIAN:__LITTLE_ENDIAN;
		Buffer tmp(ByteOrder);
		Encoder E(tmp,SqItem,ts_);//Encoder to a tmp buffer -Sam
		sentlength += E.Encode();//coding the sequence into tmp and get the length -Sam
		B<<TAG_ITEM; //Now, I can load the sq item into B, knowing the length -Sam
		B<<sentlength;
		sentlength += 8;
		B.insert(B.end(),tmp.begin(),tmp.end());
		return sentlength;
	}
	UINT32 Encoder::SendSequence(const Sequence& sequence,bool explicit_length)
	{
		if(sequence.size()==0)
			return WriteLengthAndVR(UINT32(0),VR_SQ);

		UINT32 sentlength=0;
		if(!explicit_length)
		{
			WriteLengthAndVR(UNDEFINED_LENGTH,VR_SQ);

			//Encoding the sequence item of undefined length sequence. In Part 5 Table 7.5-3,
			//undfined length sequence can contain explicit length sequence item. However, I 
			//will only implement undefined length item here for simpicity. -Sam Shen
			for(Sequence::const_iterator I=sequence.begin();I!=sequence.end();I++)
			{
				buffer_ << TAG_ITEM;
				buffer_<<UNDEFINED_LENGTH;
				Encoder E(buffer_,*I,ts_);
				E.Encode();
				buffer_ << TAG_ITEM_DELIM_ITEM;
				buffer_<<UINT32(0x00);
			}
			buffer_ << TAG_SEQ_DELIM_ITEM;
			buffer_<<UINT32(0x00);
			return UNDEFINED_LENGTH;
		}
		else//explicit length
		{
			//Create a new buffer
			//send sequence to this new buffer util done and record the length of new buffer
			//send the vr and length to original buffer
			//append the new buffer to original buffer
			int ByteOrder=ts_.isBigEndian()?__BIG_ENDIAN:__LITTLE_ENDIAN;
			Buffer B(ByteOrder);
			for(Sequence::const_iterator I=sequence.begin();I!=sequence.end();I++)
				sentlength += SendSequenceItemInExplicitLength(B,*I);
			sentlength += WriteLengthAndVR(sentlength,VR_SQ);
			buffer_.insert(buffer_.end(),B.begin(),B.end());//length has been added in the send sq item
			return sentlength;
		}
	}

	UINT32 WriteToBuffer(const DataSet& data, Buffer& buffer, TS transfer_syntax)
	{
		if(transfer_syntax.getUID() == ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX)
		{
			DataSet encodedData = EncodeEncapsulatedUncompressedPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
		}
		if(transfer_syntax.getUID() == DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_ZLIB
			Buffer explicitLittleEndian(__LITTLE_ENDIAN);
			Encoder E(explicitLittleEndian,data,TS(EXPL_VR_LE_TRANSFER_SYNTAX));
			E.Encode();
			const size_t originalSize = buffer.size();
			DeflateBuffer(explicitLittleEndian,buffer);
			return static_cast<UINT32>(buffer.size() - originalSize);
#else
			throw exception("Deflated Explicit VR Little Endian requires DICOMLIB_WITH_ZLIB");
#endif
		}
		if(transfer_syntax.getUID() == RLE_LOSSLESS_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_RLE
			DataSet encodedData = EncodeRLELosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("RLE Lossless requires DICOMLIB_WITH_RLE");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_BASELINE_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEG
			DataSet encodedData = EncodeJPEGBaselinePixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG Baseline requires DICOMLIB_WITH_JPEG");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_LOSSLESS_NON_HIERARCHICAL)
		{
#if DICOMLIB_WITH_GDCM
			std::vector<Value> fragments = data.Values(TAG_PIXEL_DATA);
			Enforce(!fragments.empty(), "JPEG Lossless Process 14 SV1 Pixel Data has no fragments");
			for(size_t i=0;i<fragments.size();++i)
				Enforce(fragments[i].vr() == VR_OB, "JPEG Lossless Process 14 SV1 fragments must be OB");
			Encoder E(buffer,data,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG Lossless Process 14 SV1 requires DICOMLIB_WITH_GDCM");
#endif
		}
		if(transfer_syntax.getUID() == JPEG2000_LOSSLESS_ONLY || transfer_syntax.getUID() == JPEG2000)
		{
#if DICOMLIB_WITH_JPEG2000
			DataSet encodedData = EncodeJPEG2000LosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG 2000 requires DICOMLIB_WITH_JPEG2000");
#endif
		}
		if(transfer_syntax.getUID() == HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_HTJ2K
			DataSet encodedData = EncodeHTJ2KLosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
		}
		if(transfer_syntax.getUID() == HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_HTJ2K
			DataSet encodedData = EncodeHTJ2KRPCLLosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
		}
		if(transfer_syntax.getUID() == HTJ2K_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_HTJ2K
			DataSet encodedData = EncodeHTJ2KPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("HTJ2K requires DICOMLIB_WITH_HTJ2K");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_LS_LOSSLESS_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEGLS
			DataSet encodedData = EncodeJPEGLSLosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG-LS requires DICOMLIB_WITH_JPEGLS");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEGLS
			DataSet encodedData = EncodeJPEGLSNearLosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG-LS requires DICOMLIB_WITH_JPEGLS");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_XL_LOSSLESS_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEGXL
			DataSet encodedData = EncodeJPEGXLLosslessPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_XL_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEGXL
			DataSet encodedData = EncodeJPEGXLPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
		}
		if(transfer_syntax.getUID() == JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX)
		{
#if DICOMLIB_WITH_JPEGXL
			DataSet encodedData = EncodeJPEGXLJPEGRecompressionPixelData(data);
			Encoder E(buffer,encodedData,transfer_syntax);
			return E.Encode();
#else
			throw exception("JPEG XL requires DICOMLIB_WITH_JPEGXL");
#endif
		}
		Encoder E(buffer,data,transfer_syntax);
		return E.Encode();
	}
}//namespace dicom
