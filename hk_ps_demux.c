#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "hk_ps_demux.h"
#include "hk_ps_demux_priv.h"
#include "avc_hevc_analyse.h"

#ifdef __cplusplus
extern "C" {
#endif 


/**
* @brief: 初始化PS解封装上下文
*
* @filename[in]: PS文件
*
* @return: PS解封装句柄
*
* @note.返回NULL为失败
**/
Handle HK_PS_Open(char *file)
{
	int ret;

	if (file == NULL)
		goto EXIT;
	hkps_demux_ctx_s *pCtx = (hkps_demux_ctx_s *)malloc(sizeof(hkps_demux_ctx_s));	
	memset(pCtx, 0, sizeof(hkps_demux_ctx_s));

	pCtx->fp = fopen(file, "r");
	if (pCtx->fp == NULL)
	{
		HK_PS_LOG_ERR("%s fopen failed!%s\n", file, strerror(errno));
		goto EXIT;
	}

	pCtx->name = malloc(strlen(file)+1);
	strncpy(pCtx->name, file, strlen(file)+1);

	pCtx->cache_size = CACHE_PS_PIECE_SIZE;
	pCtx->pcache     = (uint8_t *)malloc(CACHE_PS_PIECE_SIZE);

	pCtx->vidRawbuffsize = VIDEO_RAWDATA_BUFF_SIZE;
	pCtx->vidRawbuff = (uint8_t *)malloc(VIDEO_RAWDATA_BUFF_SIZE);

	//Get PS info start
	uint8_t *frame = NULL;
	int      framelen;
	if (HK_PS_EFAIL == HK_PS_ReadFrame(pCtx, (void **)&frame, &framelen))
	{
		HK_PS_LOG_ERR("%s read frame failed!\n", pCtx->name);
		goto EXIT;
	}
	HK_PS_ReleaseFrame(pCtx, frame);
	pCtx->start_time = pCtx->videoTimestamp;

	fseek(pCtx->fp, -(VIDEO_RAWDATA_BUFF_SIZE*2), SEEK_END);
	pCtx->cache_data_len  = 0;
	pCtx->cache_pos       = 0;
	while(1)
	{
		ret = HK_PS_ReadFrame(pCtx, (void **)&frame, &framelen);
		if (HK_PS_SOK != ret)
			break;
		pCtx->end_time = pCtx->videoTimestamp;
		HK_PS_ReleaseFrame(pCtx, frame);
	}

	if (pCtx->end_time < pCtx->start_time)
	{
		HK_PS_LOG_ERR("%s end_time maybe overflow (%#lx < %#lx), try to repair!\n", pCtx->name, pCtx->end_time, pCtx->start_time);
		if (pCtx->start_time > 0xFFFFFFFF - PS_DURATION_OVERFLOW_THD)
		{//开始时间戳溢出1h以内，认为是时间戳溢出的异常
			HK_PS_LOG_WARN("%s pst overflow try to repair!\n", pCtx->name);
			pCtx->end_time += (0xFFFFFFFF / 90);
		}
		else{//否则通过累积时间戳计算
			HK_PS_LOG_WARN("%s pst overflow, use time_mode1!\n", pCtx->name);
			pCtx->timestamp_mode = 1;
			pCtx->accumulateTime = 0;
			pCtx->cache_data_len  = 0;
			pCtx->cache_pos       = 0;
			fseek(pCtx->fp, 0, SEEK_SET);
			pCtx->start_time = 0;
			pCtx->end_time	 = 0;
			while(1)
			{
				ret = HK_PS_ReadFrame(pCtx, (void **)&frame, &framelen);
				if (HK_PS_SOK != ret)
					break;
				pCtx->end_time = pCtx->videoTimestamp;
				HK_PS_ReleaseFrame(pCtx, frame);
			}
		}

	}
	pCtx->duration_time = pCtx->end_time - pCtx->start_time;
	//Get PS info end
	pCtx->corrTimestamp  = 0;
	pCtx->accumulateTime = 0;
	pCtx->lastTimestamp  = 0;
	pCtx->curTimestamp   = 0;
	pCtx->cache_data_len = 0;
	pCtx->cache_pos      = 0;	
	fseek(pCtx->fp, 0, SEEK_SET);

	HK_PS_LOG_INFO("%s open success![%d * %d * %d] during:%lu start:%lu end:%lu\n", pCtx->name, pCtx->width, pCtx->height, pCtx->fps, pCtx->duration_time,
												pCtx->start_time, pCtx->end_time);
	return pCtx;

EXIT:
	if (pCtx->fp)
		fclose(pCtx->fp);
	if (pCtx->pcache)
		free(pCtx->pcache);
	if (pCtx->vidRawbuff)
		free(pCtx->vidRawbuff);
	free(pCtx);
	
	return NULL;
}

/**
* @brief: 销毁PS解封装上下文
*
* @Handle[in]: PS句柄
*
* @return: succes 0 / fail -1
*
* @note.
**/
int HK_PS_Close(Handle hdl)
{
	hkps_demux_ctx_s *pCtx = hdl;
	fclose(pCtx->fp);
	free(pCtx->vidRawbuff);
	free(pCtx->pcache);
	free(pCtx->name);
	free(pCtx);

	return 0;
}

/**
* @brief: 获取PS码流信息
*
* @Handle[in]: PS句柄
* @fileinfo[out]: 码流信息
*
* @return: succes 0 / fail -1
*
* @note.
**/
int HK_PS_GetFileInfo(Handle hdl, hkps_file_info_s *fileinfo)
{
	hkps_demux_ctx_s *pCtx = (hkps_demux_ctx_s *)hdl;
	
	memset(fileinfo, 0, sizeof(hkps_file_info_s));

	fileinfo->start_time = pCtx->start_time; // ms
	fileinfo->end_time = pCtx->end_time; // ms
	fileinfo->streams[PS_STREAMID_VIDEO].enType = 1;
//	fileinfo->streams[PS_STREAMID_VIDEO].codecType = HKPS_CodectypeConvert(pctx->codec);
	fileinfo->streams[PS_STREAMID_VIDEO].info.video.width = pCtx->width;
	fileinfo->streams[PS_STREAMID_VIDEO].info.video.height = pCtx->height;
	fileinfo->streams[PS_STREAMID_VIDEO].info.video.fps = pCtx->fps>0?pCtx->fps:25;

	return HK_PS_SOK;
}

static int update_cache_buffer(hkps_demux_ctx_s *pCtx)
{
	uint32_t datalen;
	
	if (pCtx->cache_pos < pCtx->cache_data_len)
	{//移动前一分片剩余数据
		memmove(pCtx->pcache, pCtx->pcache+pCtx->cache_pos, pCtx->cache_data_len-pCtx->cache_pos);
	}
	pCtx->cache_data_len = pCtx->cache_data_len-pCtx->cache_pos;
	datalen = fread(pCtx->pcache+pCtx->cache_data_len, 1, pCtx->cache_size-pCtx->cache_data_len, pCtx->fp);
	if (datalen == 0)
	{
		return HK_PS_EOF;
	}

	pCtx->cache_pos = 0;
	pCtx->cache_data_len += datalen;

	return HK_PS_SOK;
}

/**
* @brief: 读取一帧数据
*
* @Handle[in]: PS句柄
* @frmdata[out]: 帧数据
* @frmlen[out]: 帧长度
*
* @return: STREAM_OK 0 / STREAM_FAIL -1
*
* @note.STREAM_EOF文件读结束;配套HK_PS_ReleaseFrame使用
**/
int HK_PS_ReadFrame(Handle hdl,void **frmdata, int *frmlen)
{
	//I帧：PS_Header + System_Header + Program_Stream_map + PES(video/audio) + PES(video/audio) + ...
	//P帧：PS_Header + PES(video/audio) + PES(video/audio) + ...
	hkps_demux_ctx_s *pCtx = (hkps_demux_ctx_s *)hdl;

	//Parse PS_Header
	uint8_t *pdata = NULL;
	uint32_t offset = 0;
	uint8_t findPsflag = 0;
	while(1)
	{
		if (update_cache_buffer(pCtx) != HK_PS_SOK)
		{
			HK_PS_LOG_INFO("%s is read eof!\n", pCtx->name);
			return HK_PS_EOF;
		}

		//find and parse ps_header
		char scode[4] = {0x00, 0x00, 0x01, PS_HEADER_SCODE};
		for(offset = pCtx->cache_pos; offset < pCtx->cache_data_len; offset++)
		{
			pdata = pCtx->pcache + offset;
			if (0 == memcmp(pdata, &scode, 4))
			{
				findPsflag = 1;
				break;
			}
		}
		
		pCtx->cache_pos = offset;  //定位到PS头
		if (findPsflag == 0 || pdata == NULL || pCtx->cache_data_len < offset + sizeof(ps_header_s))
		{
			continue;
		}
		
		ps_header_s pshead = *(ps_header_s *)pdata;
		pshead.stuffing_len = pdata[13]&0x07; //little-endian repair
		if (pshead.stuffing_len != 0 && pCtx->cache_data_len < offset + pshead.stuffing_len + sizeof(ps_header_s)){
			continue;
		}
		offset = offset + sizeof(ps_header_s) + pshead.stuffing_len; //跳过PS_Header
		
		///find and parse System_Header
		//当且仅当PS包是第一个数据包时才存在
		if (pCtx->cache_data_len  < offset + sizeof(ps_sys_header_s))
		{
			continue;
		}
		ps_sys_header_s ssyshead = *(ps_sys_header_s *)(pCtx->pcache + offset);
		scode[3] = PS_SYSTEM_HEADER_SCODE;
		ssyshead.header_len = (ssyshead.header_len >> 8) | (ssyshead.header_len<<8);
		if (0 == memcmp(&ssyshead.header_start_code, &scode, sizeof(ssyshead.header_start_code)))	
		{
			offset = offset + sizeof(ssyshead.header_start_code) + sizeof(ssyshead.header_len) + ssyshead.header_len;	
		}
		
		//Program Stream map
		if (pCtx->cache_data_len < offset + sizeof(program_stream_map_s))
		{
			continue;
		}
		program_stream_map_s pspsmhead = *(program_stream_map_s *)(pCtx->pcache + offset);
		scode[3] = PS_PSM_HEADER_SCODE;
		if (0 == memcmp(&pspsmhead.packet_start_code, &scode, 4))
		{//find PSM, Parse it
			pspsmhead.psm_len = (pspsmhead.psm_len >> 8)|(pspsmhead.psm_len << 8);
			if (pCtx->cache_data_len < offset + sizeof(pspsmhead.packet_start_code) + sizeof(pspsmhead.map_stream_id) + sizeof(pspsmhead.psm_len) + pspsmhead.psm_len)
			{
				continue;
			}
			uint8_t *pu8Psm = pCtx->pcache + offset;
			uint16_t program_stream_info_len = ((uint16_t)pu8Psm[8] << 8)|(pu8Psm[9]);
			pu8Psm = pu8Psm + 10 + program_stream_info_len;  //elementarty_stream_map start
			uint16_t elementary_stream_map_length = ((uint16_t)pu8Psm[0] << 8)|pu8Psm[1]; //=sizeof(stream_type + elementary_stream_id + elementary_stream_info_len) * N = 4 * N
			uint16_t elementary_num = elementary_stream_map_length / 4;
			HK_PS_LOG_DEBUG("%s find PSM element_num:%d \n", pCtx->name, elementary_num);
			pu8Psm = pu8Psm+sizeof(elementary_stream_map_length);
			for (int i=0; i < elementary_num; i++)
			{
				/* by GB/T28181-2022
				* stream_type: H.264  0x1B
				*              H.265  0x24
				*              MPEG-4 0x10
				*              SVAC   0x80
				*              G.711A 0x90 [G.711U 0x91] [G722.1 0x92] [G.723.1 0x93] [SVAC_Audio 0x9B] [AAC 0x1F]
				*  
				* stream_id:   Video_ID: 0xE0
				*              Audio_ID: 0xC0
				*/
				uint8_t stream_type = pu8Psm[4*i];
				uint8_t stream_id   = pu8Psm[4*i+1];
				uint16_t stream_info_len   = (pu8Psm[4*i+2] << 8)|pu8Psm[4*i+3];
				pu8Psm = pu8Psm + 4 + stream_info_len;
				if (stream_id == 0xE0)
				{
					if (stream_type != PS_PLAYLOAD_H264 && stream_type != PS_PLAYLOAD_H264)
					{
						HK_PS_LOG_WARN("%s PSM video id:%#x type:%#x not support!\n", pCtx->name, stream_id, stream_type);
						continue;
					}
					pCtx->psm_stream_video_id = stream_id;
					pCtx->psm_stream_video_type = stream_type;
					HK_PS_LOG_DEBUG("%s PSM video id:%#x type:%#x\n", pCtx->name, stream_id, stream_type);
				}
				else if (stream_id == 0xC0)
				{
					pCtx->psm_stream_audio_id = stream_id;
					pCtx->psm_stream_audio_type = stream_type;
					HK_PS_LOG_DEBUG("%s PSM audio id\n", pCtx->name);
				}	
				else{
					HK_PS_LOG_DEBUG("%s i:%d stream_id=%x stream_id=%x not support!\n", pCtx->name, i, stream_type, stream_id)
					continue;
				}
			}
			
			offset = offset + sizeof(pspsmhead.packet_start_code) + sizeof(pspsmhead.map_stream_id) + sizeof(pspsmhead.psm_len) + pspsmhead.psm_len; //PSM End
		}

		if (pCtx->psm_stream_video_id == 0)  //第一帧需要找到I帧
		{
			pCtx->cache_pos = offset;
			continue;
		}
		while(1)
		{//其余海康私有头丢弃，直到遇到PES包
			if (pCtx->pcache[offset] == 0x00 && pCtx->pcache[offset+1] == 0x00 && pCtx->pcache[offset+2] == 0x01
				&& (pCtx->pcache[offset+3] == pCtx->psm_stream_audio_id || pCtx->pcache[offset+3] == pCtx->psm_stream_video_id))
			{
				HK_PS_LOG_DEBUG("%s find pes_head_type=%x\n", pCtx->name, pCtx->pcache[offset+3]);
				pCtx->cache_pos = offset;  //找到PES包后，POS到PES包
				break;			
			}

			uint16_t priv_head_len = (pCtx->pcache[offset+4] << 8)|pCtx->pcache[offset+5];
			if (pCtx->cache_data_len < offset + 6 + priv_head_len)
			{
				pCtx->cache_pos = offset; //数据不够，更新POS加载数据
				if (update_cache_buffer(pCtx) != HK_PS_SOK)
				{
					HK_PS_LOG_INFO("%s is read eof!\n", pCtx->name);
					return HK_PS_EOF;
				}
				continue;
			}
			HK_PS_LOG_DEBUG("%s find priv_head_type=%x\n", pCtx->name, pCtx->pcache[offset+3]);			
			offset += (6 + priv_head_len); //跳过私有头
		}
		
		break;
	}

	//PES Packet
	uint8_t flgfindframe = 0;
	uint16_t pes_packet_len = 0;

	offset = pCtx->cache_pos;
	pCtx->vidRawbuffpos = 0;
	while(1)
	{	
		//PES Header
		if (!(pCtx->pcache[offset] == 0x00 && pCtx->pcache[offset+1] == 0x00 && pCtx->pcache[offset+2] == 0x01
			&& (pCtx->pcache[offset+3] == pCtx->psm_stream_audio_id || pCtx->pcache[offset+3] == pCtx->psm_stream_video_id)))
		{
			pCtx->cache_pos = offset;
			
			if (flgfindframe)
			{
				goto TRANS_PACKET;
			}
			else{
				HK_PS_LOG_ERR("%s find pes head[%x %x %x %x] failed!\n", pCtx->name, pCtx->pcache[offset], pCtx->pcache[offset+1], pCtx->pcache[offset+2], pCtx->pcache[offset+3]);
				return HK_PS_EFAIL;
			}
		}

		pes_packet_len = (pCtx->pcache[offset+4] << 8)|pCtx->pcache[offset+5];
		if (pCtx->cache_data_len < offset + 6 + pes_packet_len)
		{
			if (update_cache_buffer(pCtx) != HK_PS_SOK)
			{
				HK_PS_LOG_INFO("%s is read eof!\n", pCtx->name);
				return HK_PS_EOF;
			}
			offset = pCtx->cache_pos;
		}
		if (pCtx->pcache[offset+3] == pCtx->psm_stream_video_id)
		{
			uint8_t PTS_DTS_flags = pCtx->pcache[offset+7]&0xC0;
			uint8_t PES_header_data_length = pCtx->pcache[offset+8];

			/*
			* '0010'      --4
			* PTS[32..30] --3
			* marker_bit  --1
			* PTS[29..15] --15
			* marker_bit  --1
			* PTS[14..0]  --15
			* marker_bit  --1
			*/
			if (PTS_DTS_flags & 0x80)
			{
				unsigned char *pts_buf = pCtx->pcache + offset + 9;
				uint64_t pts = ((unsigned long long )(pts_buf[0] & 0x0E)) << 29;
				pts |= pts_buf[1] << 22;
				pts |= (pts_buf[2] & 0xFE) << 14;
				pts |= pts_buf[3] << 7;
				pts |= (pts_buf[4] & 0xFE) >> 1;
				pts = pts / 90;

				pCtx->curTimestamp = pts + pCtx->corrTimestamp;
				if (pCtx->lastTimestamp > pCtx->curTimestamp)
				{
					HK_PS_LOG_ERR("%s pst error! last:%#lx cur:%#lx corr:%lx!\n", pCtx->name, pCtx->lastTimestamp, pCtx->curTimestamp, pCtx->corrTimestamp);
					if (pCtx->lastTimestamp > 0xFFFFFFFF-PS_DIFF_OVERFLOW_THD) //时间戳溢出情形判定
					{
						HK_PS_LOG_WARN("%s pst overflow try to repair!\n", pCtx->name);
						pCtx->corrTimestamp = pCtx->lastTimestamp;
						pCtx->curTimestamp = pts + pCtx->corrTimestamp;
					}
				}

                HK_PS_LOG_DEBUG("[%s]video pes timestamp[%lu]\n",  pCtx->name, pCtx->curTimestamp);
                if(pCtx->lastTimestamp != pCtx->curTimestamp)
                {
                    int diff = pCtx->curTimestamp - pCtx->lastTimestamp;
                    if( diff > 0 && diff < 1000 )
                    {
                        pCtx->accumulateTime += diff;
                    }else{
						pCtx->accumulateTime += 40;
					}

                    pCtx->lastTimestamp = pCtx->curTimestamp;
                }		

				if(pCtx->timestamp_mode == 0)
		        	pCtx->videoTimestamp = pCtx->curTimestamp;
				else
					pCtx->videoTimestamp = pCtx->accumulateTime;
			}
			//忽略DTS
			//PES Header END

			//定位到PES数据（RAW Data）
			uint8_t *pNalu =  pCtx->pcache + offset + 9 + PES_header_data_length;
			uint32_t rawlen = pes_packet_len - 3 - PES_header_data_length;   //Raw数据长度=PES包长度-PES头长度
			if (pCtx->vidRawbuffpos+rawlen < pCtx->vidRawbuffsize)
			{//拷贝到临时缓存区，多PES包数据通过Pos偏移取出
				memcpy(pCtx->vidRawbuff+pCtx->vidRawbuffpos, pNalu, rawlen);
				pCtx->vidRawbuffpos += rawlen;
			}
			else
			{
				HK_PS_LOG_ERR("%s pos:%d framelen:%d too long!\n", pCtx->name, pCtx->vidRawbuffpos, rawlen)
			}
			flgfindframe = 1;
		}
		else if (pCtx->pcache[offset+3] == pCtx->psm_stream_audio_id)
		{
			;//忽略音频包
		}
		else{
			;//忽略 PES_Packet_date_byte || padding_byte
		}
		
		//PES Body
		offset = offset + 6 + pes_packet_len;
		continue;
		
TRANS_PACKET:
		//视频参数解析
		if(pCtx->psm_stream_video_type == PS_PLAYLOAD_H264){
			if (pCtx->vidRawbuff[0] == 0x00 && pCtx->vidRawbuff[1] == 0x00 && pCtx->vidRawbuff[2] == 0x00 
				&& pCtx->vidRawbuff[3] == 0x01 && ((pCtx->vidRawbuff[4]&0x1F)==7 || (pCtx->vidRawbuff[4]&0x1F)==0x0F)){
				//H.264 I帧一般以SPS NALU打头
				struct video_sps_param_s spsparm;
				if (HK_PS_SOK == H264_SPS_Analyse(pCtx->vidRawbuff, pCtx->vidRawbuffpos, &spsparm))
				{
					pCtx->width  = spsparm.width;
					pCtx->height = spsparm.height;
					pCtx->fps    = spsparm.fps;
				}
				pCtx->isCurKey = 1;
            }
			else
				pCtx->isCurKey = 0;
		}
		else if(pCtx->psm_stream_video_type == PS_PLAYLOAD_H265){
			if (pCtx->vidRawbuff[0] == 0x00 && pCtx->vidRawbuff[1] == 0x00 && pCtx->vidRawbuff[2] == 0x00 
				&& pCtx->vidRawbuff[3] == 0x01 && ((pCtx->vidRawbuff[4] & 0x7E) == 0x40)){
				//H.265 I帧一般以VPS NALU打头
				int idx = 0;
				while(idx++ < pCtx->vidRawbuffpos){
					if (pCtx->vidRawbuff[idx] == 0x00 && pCtx->vidRawbuff[idx+1] == 0x00 && pCtx->vidRawbuff[idx+2] == 0x00 
						&& pCtx->vidRawbuff[idx+3] == 0x01 && ((pCtx->vidRawbuff[idx+4] & 0x7E) == 0x42)){//识别到SPS
						struct video_sps_param_s spsparm;
						if (HK_PS_SOK == H265_SPS_Analyse(pCtx->vidRawbuff+idx, pCtx->vidRawbuffpos-idx, &spsparm))
						{
							pCtx->width  = spsparm.width;
							pCtx->height = spsparm.height;	
							pCtx->fps    = spsparm.fps;
						}
						break;
					}
				}
				pCtx->isCurKey = 1;
			}
			else
				pCtx->isCurKey = 0;
		}
		else
		{
			HK_PS_LOG_ERR("%s meet payload=%#x not support!\n", pCtx->name, pCtx->psm_stream_video_type);
			return HK_PS_EFAIL;
		}
		
		//PES->Raw Video Stream
		*frmdata = malloc(pCtx->vidRawbuffpos);
		memcpy(*frmdata, pCtx->vidRawbuff, pCtx->vidRawbuffpos);
		*frmlen = pCtx->vidRawbuffpos;
		
		break;
	}

	return HK_PS_SOK;
}

/**
* @brief: 释放一帧数据
*
* @Handle[in]: PS句柄
* @frmdata[int]: 帧数据
*
* @return: succes 0 / fail -1
*
* @note.
**/
int HK_PS_ReleaseFrame(Handle hdl,void *frmdata)
{
	if (frmdata)
		free(frmdata);
	
	return HK_PS_SOK;
}


/**
* @brief: 按照比例跳转最近I帧数据位置
*
* @Handle[in]: PS句柄
* @pos[in]: start_time~end_time 单位ms
* @mode[in]: seek模式 0:forward 1:backward
*
* @return: succes 0 / fail -1
*
* @note.
**/
int HK_PS_SeekPos(Handle hdl, long pos, int mode)
{
	hkps_demux_ctx_s *pCtx = (hkps_demux_ctx_s *)hdl;
	int ret;
	uint8_t *frame;
	int framelen;
	
	if (pos < pCtx->start_time || pos > pCtx->end_time)
	{
		HK_PS_LOG_ERR("%s pos:%ld over limit[%lu, %lu]!\n", pCtx->name, pos, pCtx->start_time, pCtx->end_time);
		return HK_PS_EFAIL;
	}
	
	pCtx->seek_time = pos;
	pCtx->seek_mode = mode;
	while(1)
	{
		ret = HK_PS_ReadFrame(pCtx, (void **)&frame, &framelen);
		if (HK_PS_SOK != ret)
			break;

		if (!pCtx->isCurKey)
			goto NEXT;  //定位I帧

		if (pCtx->seek_mode == 0) //forward
		{
			if (pCtx->videoTimestamp > pCtx->seek_time)
				break;
		}
		else if (pCtx->seek_mode == 1) //backward
		{
			int64_t diff = (int64_t)(pCtx->seek_time - pCtx->videoTimestamp);
			if (diff < 2000) //I帧间隔小于2秒认为定位完成
				break;
		}
		else
		{
			HK_PS_LOG_ERR("unsupport seek mode(%u)\n", pCtx->seek_mode);
		}

	NEXT:
		HK_PS_ReleaseFrame(pCtx, frame);
	}
	
	HK_PS_LOG_INFO("%s find start timestamp:%lu!\n", pCtx->name, pCtx->videoTimestamp);

	return HK_PS_SOK;

}

#ifdef __cplusplus
}
#endif




