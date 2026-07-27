#ifndef HPP_INCLUDE_GUARD_57343948
#define HPP_INCLUDE_GUARD_57343948

#include <string>
#include <vector>
#include "UID.hpp"
namespace dicom
{
	/*
		Should the following constants be strings or UIDs?
	*/

	/*
		Should these be in a sub-namespace?
	*/



	/*
		transfer syntaxes
	*/
	//!Implicit VR, Little Endian
	const UID IMPL_VR_LE_TRANSFER_SYNTAX			= UID("1.2.840.10008.1.2");

	//!Explicit VR, Little Endian
	const UID EXPL_VR_LE_TRANSFER_SYNTAX			= UID("1.2.840.10008.1.2.1");

	//!Encapsulated Uncompressed Explicit VR Little Endian
	const UID ENCAPSULATED_UNCOMPRESSED_EXPL_VR_LE_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.1.98");

	//!Deflated Image Frame Compression
	const UID DEFLATED_IMAGE_FRAME_COMPRESSION_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.8.1");

	//!Not sure what this is!  Probably shouldn't advertise as supporting it!
	const UID DEFLATED_EXPL_VR_LE_TRANSFER_SYNTAX   = UID("1.2.840.10008.1.2.1.99");

	//!Explicit VR, Big Endian
	const UID EXPL_VR_BE_TRANSFER_SYNTAX			= UID("1.2.840.10008.1.2.2");

	//!SMPTE ST 2110-20 Uncompressed Progressive Active Video
	const UID SMPTE_ST_2110_20_UNCOMPRESSED_PROGRESSIVE_ACTIVE_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.7.1");

	//!SMPTE ST 2110-20 Uncompressed Interlaced Active Video
	const UID SMPTE_ST_2110_20_UNCOMPRESSED_INTERLACED_ACTIVE_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.7.2");

	//!SMPTE ST 2110-30 PCM Digital Audio
	const UID SMPTE_ST_2110_30_PCM_DIGITAL_AUDIO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.7.3");


	//JPEG encoding:

	//!Baseline JPEG Compression (coding Process 1)
	/*!
		This must be supported by an implementation that supports any 8-bit lossy compression.
		See Part 5 Section 8.2.1
	*/
	const UID JPEG_BASELINE_TRANSFER_SYNTAX 		= UID("1.2.840.10008.1.2.4.50");

	//!JPEG Extended compression (coding processes 2 and 4)
	const UID JPEG_EXTENDED_PROCESS_2_4_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.51");

	//!JPEG Extended compression (coding processes 3 and 5) (Retired)
	const UID JPEG_EXTENDED_PROCESS_3_5_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.52");

	//!JPEG Spectral Selection, Non-Hierarchical (coding processes 6 and 8) (Retired)
	const UID JPEG_SPECTRAL_SELECTION_PROCESS_6_8_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.53");

	//!JPEG Full Progression, Non-Hierarchical (coding processes 10 and 12) (Retired)
	const UID JPEG_FULL_PROGRESSION_PROCESS_10_12_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.55");

	//!JPEG Lossless compression (coding process 14)
	const UID JPEG_LOSSLESS_PROCESS_14_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.57");

	//!Lossless JPEG compression (coding process 14, first-order horizontal prediction)
	/*!
		This must be supported by an implementation that supports any lossless compression.
		See Part 5 Section 8.2.1
	*/
		//has to be supported if we do any lossless jpeg, see note 
	const UID JPEG_LOSSLESS_NON_HIERARCHICAL		= UID("1.2.840.10008.1.2.4.70");

	//!JPEG-LS Lossless Image Compression
	const UID JPEG_LS_LOSSLESS_TRANSFER_SYNTAX		= UID("1.2.840.10008.1.2.4.80");

	//!JPEG-LS Lossy (Near-Lossless) Image Compression
	const UID JPEG_LS_NEAR_LOSSLESS_TRANSFER_SYNTAX	= UID("1.2.840.10008.1.2.4.81");

	//!JPEG2000LosslessOnly (Part5 Annex A.4.4, first type
	const UID JPEG2000_LOSSLESS_ONLY				= UID("1.2.840.10008.1.2.4.90");

	/*
		There are more to still go in here, mostly to do with JPEG encoding.
		These are in the range 1.2.840.10008.1.2.4.50 to 1.2.840.10008.1.2.4.70

		Part 5, Annex A.4 says that the entire dataset in these cases is encoded
		Little Endian, Explicit VR, and then provides descriptions of how pixel
		data is to be encoded.  This is outside the scope of this library,
		but we should at least recognize the transfer syntaxes

	*/

    const UID JPEG2000                              = UID("1.2.840.10008.1.2.4.91");

	//!JPIP Referenced Transfer Syntax
	const UID JPIP_REFERENCED_TRANSFER_SYNTAX		= UID("1.2.840.10008.1.2.4.94");

	//!JPIP Referenced Deflate Transfer Syntax
	const UID JPIP_REFERENCED_DEFLATE_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.95");

	//!MPEG2 Main Profile / Main Level Video Compression
	const UID MPEG2_MAIN_PROFILE_MAIN_LEVEL_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.100");

	//!Fragmentable MPEG2 Main Profile / Main Level Video Compression
	const UID FRAGMENTABLE_MPEG2_MAIN_PROFILE_MAIN_LEVEL_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.100.1");

	//!MPEG2 Main Profile / High Level Video Compression
	const UID MPEG2_MAIN_PROFILE_HIGH_LEVEL_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.101");

	//!Fragmentable MPEG2 Main Profile / High Level Video Compression
	const UID FRAGMENTABLE_MPEG2_MAIN_PROFILE_HIGH_LEVEL_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.101.1");

	//!MPEG-4 AVC/H.264 High Profile / Level 4.1 Video Compression
	const UID MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.102");

	//!Fragmentable MPEG-4 AVC/H.264 High Profile / Level 4.1 Video Compression
	const UID FRAGMENTABLE_MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.102.1");

	//!MPEG-4 AVC/H.264 BD-compatible High Profile / Level 4.1 Video Compression
	const UID MPEG4_AVC_H264_BD_COMPATIBLE_HIGH_PROFILE_LEVEL_4_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.103");

	//!Fragmentable MPEG-4 AVC/H.264 BD-compatible High Profile / Level 4.1 Video Compression
	const UID FRAGMENTABLE_MPEG4_AVC_H264_BD_COMPATIBLE_HIGH_PROFILE_LEVEL_4_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.103.1");

	//!MPEG-4 AVC/H.264 High Profile / Level 4.2 For 2D Video Compression
	const UID MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_2_2D_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.104");

	//!Fragmentable MPEG-4 AVC/H.264 High Profile / Level 4.2 For 2D Video Compression
	const UID FRAGMENTABLE_MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_2_2D_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.104.1");

	//!MPEG-4 AVC/H.264 High Profile / Level 4.2 For 3D Video Compression
	const UID MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_2_3D_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.105");

	//!Fragmentable MPEG-4 AVC/H.264 High Profile / Level 4.2 For 3D Video Compression
	const UID FRAGMENTABLE_MPEG4_AVC_H264_HIGH_PROFILE_LEVEL_4_2_3D_VIDEO_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.105.1");

	//!MPEG-4 AVC/H.264 Stereo High Profile / Level 4.2 Video Compression
	const UID MPEG4_AVC_H264_STEREO_HIGH_PROFILE_LEVEL_4_2_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.106");

	//!Fragmentable MPEG-4 AVC/H.264 Stereo High Profile / Level 4.2 Video Compression
	const UID FRAGMENTABLE_MPEG4_AVC_H264_STEREO_HIGH_PROFILE_LEVEL_4_2_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.106.1");

	//!HEVC/H.265 Main Profile / Level 5.1 Video Compression
	const UID HEVC_H265_MAIN_PROFILE_LEVEL_5_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.107");

	//!HEVC/H.265 Main 10 Profile / Level 5.1 Video Compression
	const UID HEVC_H265_MAIN_10_PROFILE_LEVEL_5_1_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.108");

	//!JPEG XL Lossless Image Compression
	const UID JPEG_XL_LOSSLESS_TRANSFER_SYNTAX		= UID("1.2.840.10008.1.2.4.110");

	//!JPEG XL JPEG Recompression
	const UID JPEG_XL_JPEG_RECOMPRESSION_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.111");

	//!JPEG XL Image Compression
	const UID JPEG_XL_TRANSFER_SYNTAX				= UID("1.2.840.10008.1.2.4.112");

	//!High-Throughput JPEG 2000 Image Compression (Lossless Only)
	const UID HTJ2K_LOSSLESS_ONLY_TRANSFER_SYNTAX	= UID("1.2.840.10008.1.2.4.201");

	//!High-Throughput JPEG 2000 with RPCL Options Image Compression (Lossless Only)
	const UID HTJ2K_RPCL_LOSSLESS_TRANSFER_SYNTAX	= UID("1.2.840.10008.1.2.4.202");

	//!High-Throughput JPEG 2000 Image Compression
	const UID HTJ2K_TRANSFER_SYNTAX					= UID("1.2.840.10008.1.2.4.203");

	//!JPIP HTJ2K Referenced Transfer Syntax
	const UID JPIP_HTJ2K_REFERENCED_TRANSFER_SYNTAX	= UID("1.2.840.10008.1.2.4.204");

	//!JPIP HTJ2K Referenced Deflate Transfer Syntax
	const UID JPIP_HTJ2K_REFERENCED_DEFLATE_TRANSFER_SYNTAX = UID("1.2.840.10008.1.2.4.205");

	const UID RLE_LOSSLESS_TRANSFER_SYNTAX			= UID("1.2.840.10008.1.2.5");



	/*
		End of transfer syntaxes
	*/

	const UID MEDIA_DIR_STORAGE_SOP_CLASS			= UID("1.2.840.10008.1.3.10");

	//!Part 7, Annex A.21 says that this is the ONLY application context name
	const UID APPLICATION_CONTEXT					= UID("1.2.840.10008.3.1.1.1");

	const UID MODALITY_PPS_SOP_CLASS				= UID("1.2.840.10008.3.1.2.3.3");


	//!Where is this defined?
	const UID VERIFICATION_SOP_CLASS	        	= UID("1.2.840.10008.1.1");


	/*
	*	Following are defined in Part 4, Table B.5-1
	*/
	const UID CR_IMAGE_STORAGE_SOP_CLASS	    		= UID("1.2.840.10008.5.1.4.1.1.1");
	const UID DX_PRES_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.1.1");
	const UID DX_PROC_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.1.1.1");
	const UID MAMMO_PRES_IMAGE_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.1.2");
	const UID MAMMO_PROC_IMAGE_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.1.2.1");
	const UID INTRAORAL_PRES_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.1.3");
	const UID INTRAORAL_PROC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.1.3.1");
	const UID CT_IMAGE_STORAGE_SOP_CLASS	    		= UID("1.2.840.10008.5.1.4.1.1.2");
	const UID ENHANCED_CT_IMAGE_STORAGE_SOP_CLASS	    = UID("1.2.840.10008.5.1.4.1.1.2.1");
	const UID USOLD_MF_IMAGE_STORAGE_SOP_CLASS  		= UID("1.2.840.10008.5.1.4.1.1.3");//retired
	const UID US_MF_IMAGE_STORAGE_SOP_CLASS  			= UID("1.2.840.10008.5.1.4.1.1.3.1");
	const UID MR_IMAGE_STORAGE_SOP_CLASS	    		= UID("1.2.840.10008.5.1.4.1.1.4");
	const UID ENHANCED_MR_IMAGE_STORAGE_SOP_CLASS	    = UID("1.2.840.10008.5.1.4.1.1.4.1");
	const UID MR_SPECTROSCOPY_STORAGE_SOP_CLASS	    	= UID("1.2.840.10008.5.1.4.1.1.4.2");
	const UID USOLD_IMAGE_STORAGE_SOP_CLASS				= UID("1.2.840.10008.5.1.4.1.1.6");//retired
	const UID US_IMAGE_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.6.1");
	const UID SC_IMAGE_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.7");
	const UID MF_SB_SC_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.7.1");
	const UID MF_GREYBITE_SC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.7.2");
	const UID MF_GREYWORD_SC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.7.3");
	const UID MF_TRUECOLOR_SC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.7.4");
	//lots of Waveform not implemented here
	const UID XA_IMAGE_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.12.1");
	const UID ENHANCED_XA_IMAGE_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.12.1.1");
	const UID XRF_IMAGE_STORAGE_SOP_CLASS				= UID("1.2.840.10008.5.1.4.1.1.12.2");
	const UID ENHANCED_XRF_IMAGE_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.12.2.1");
	const UID XA2_IMAGE_STORAGE_SOP_CLASS				= UID("1.2.840.10008.5.1.4.1.1.12.3");//retired
	const UID XA_3D_IMAGE_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.13.1.1");
	const UID XC_3D_IMAGE_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.13.1.2");
	const UID NM_IMAGE_STORAGE_SOP_CLASS		    	= UID("1.2.840.10008.5.1.4.1.1.20");
	const UID RAW_DATA_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.66");
	const UID SPATIAL_REG_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.66.1");
	const UID SPATIAL_FID_STORAGE_SOP_CLASS			    = UID("1.2.840.10008.5.1.4.1.1.66.2");
	const UID DEFORM_SPATIAL_REG_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.66.3");
	const UID SEGMENTATION_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.66.4");
	const UID REAL_WORLD_VALUE_MAP_STORAGE_SOP_CLASS    = UID("1.2.840.10008.5.1.4.1.1.67");
	const UID VL_END_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.77.1.1");
	const UID VIDEO_END_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.77.1.1.1");
	const UID VL_MICROSCOPIC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.2");
	const UID VIDEO_MICROSCOPIC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.2.1");
	const UID VL_SC_MICROSCOPIC_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.3");
	const UID VL_PHOTO_IMAGE_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.77.1.4");
	const UID VIDEO_PHOTO_IMAGE_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.77.1.4.1");
	const UID OPHTHALMIC_PHOTO8_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.5.1");
	const UID OPHTHALMIC_PHOTO16_IMAGE_STORAGE_SOP_CLASS= UID("1.2.840.10008.5.1.4.1.1.77.1.5.2");
	const UID STEREOMETRIC_RELATION_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.5.3");
	const UID OPHTHALMIC_TOMO_IMAGE_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.77.1.5.4");
	const UID BASIC_TEXT_SR_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.88.11");
	const UID ENHANCED_SR_STORAGE_SOP_CLASS				= UID("1.2.840.10008.5.1.4.1.1.88.22");
	const UID COMPREHENSIVE_SR_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.88.33");
	const UID PROCEDURE_LOG_STORAGE_SOP_CLASS		    = UID("1.2.840.10008.5.1.4.1.1.88.40");
	const UID MAMMO_CAD_SR_STORAGE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.1.88.50");
	const UID KEY_OBJ_SELECTION_SOP_CLASS		        = UID("1.2.840.10008.5.1.4.1.1.88.59");
	const UID CHEST_CAD_SR_SOP_CLASS		            = UID("1.2.840.10008.5.1.4.1.1.88.65");
	const UID XRAY_RADIATION_DOSE_SR_SOP_CLASS		    = UID("1.2.840.10008.5.1.4.1.1.88.67");
	const UID PET_IMAGE_STORAGE_SOP_CLASS				= UID("1.2.840.10008.5.1.4.1.1.128");
	const UID RT_IMAGE_STORAGE_SOP_CLASS		        = UID("1.2.840.10008.5.1.4.1.1.481.1");
	const UID RT_DOSE_STORAGE_SOP_CLASS		            = UID("1.2.840.10008.5.1.4.1.1.481.2");
	const UID RT_STRUCTURE_SET_STORAGE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.1.481.3");
	const UID RT_BEAM_TREATMENT_RC_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.481.4");
	const UID RT_PLAN_STORAGE_SOP_CLASS		            = UID("1.2.840.10008.5.1.4.1.1.481.5");
	const UID RT_BRACHY_TREATMENT_RC_STORAGE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.1.481.6");
	const UID RT_TREATMENT_SUMMARY_RC_STORAGE_SOP_CLASS = UID("1.2.840.10008.5.1.4.1.1.481.7");
	const UID RT_ION_PLAN_STORAGE_SOP_CLASS	            = UID("1.2.840.10008.5.1.4.1.1.481.8");
	const UID RT_ION_BEAM_TREATMENT_RC_STORAGE_SOP_CLASS= UID("1.2.840.10008.5.1.4.1.1.481.9");
	const UID GRAYSCALE_SOFTCOPY_PS_STORAGE_SOP_CLASS   = UID("1.2.840.10008.5.1.4.1.1.11.1");

	//!Feed in one of the above entries and get a human-readable string in return.
	std::string GetUIDName(UID StorageSOP);

	bool IsTransferSyntaxUID(UID uid);
	bool IsEncapsulatedTransferSyntaxUID(UID uid);
	std::vector<UID> GetTransferSyntaxUIDs();
	std::vector<UID> GetEncapsulatedTransferSyntaxUIDs();

	/*
		See Part 4, Section C.3.1

		Defined in Part 4, Sections C.6.1.3, C.6.2.3-1
	*/
	const UID PATIENT_ROOT_QR_FIND_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.2.1.1");
	const UID PATIENT_ROOT_QR_MOVE_SOP_CLASS		= UID("1.2.840.10008.5.1.4.1.2.1.2");
	const UID PATIENT_ROOT_QR_GET_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.2.1.3");

	const UID STUDY_ROOT_QR_FIND_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.2.2.1");
	const UID STUDY_ROOT_QR_MOVE_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.2.2.2");
	const UID STUDY_ROOT_QR_GET_SOP_CLASS			= UID("1.2.840.10008.5.1.4.1.2.2.3");

	//Retired in PS3.4 2008
	const UID PATIENT_STUDY_ONLY_QR_FIND_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.2.3.1");
	const UID PATIENT_STUDY_ONLY_QR_MOVE_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.2.3.2");
	const UID PATIENT_STUDY_ONLY_QR_GET_SOP_CLASS	= UID("1.2.840.10008.5.1.4.1.2.3.3");

	const UID MODALITY_WORKLIST_SOP_CLASS			= UID("1.2.840.10008.5.1.4.31");
	const UID GENERAL_PURPOSE_WORKLIST_SOP_CLASS	= UID("1.2.840.10008.5.1.4.32.1");
}//namespace dicom
#endif //HPP_INCLUDE_GUARD_57343948
