#ifndef __HK_PS_PRIV_H__
#define __HK_PS_PRIV_H__

#ifdef __cplusplus
extern "C" {
#endif 

#define HK_PS_LOG_DEBUG(fmt, args ...) //printf("\033[0;32;32m""WARN (%s|%d):"fmt "\033[m", __func__, __LINE__, ##args);
#define HK_PS_LOG_INFO(fmt, args ...) printf("\033[0;32;32m""WARN (%s|%d):"fmt "\033[m", __func__, __LINE__, ##args);
#define HK_PS_LOG_WARN(fmt, args ...) printf("\033[1;33m""WARN (%s|%d):"fmt "\033[m", __func__, __LINE__, ##args);
#define HK_PS_LOG_ERR(fmt, args ...)  printf("\033[0;32;31m""WARN (%s|%d):"fmt "\033[m", __func__, __LINE__, ##args);


#define  CACHE_PS_PIECE_SIZE  (1024*1024)
#define  VIDEO_RAWDATA_BUFF_SIZE  (1024*1024)
#define  PS_DIFF_OVERFLOW_THD     (2*1000*90)
#define  PS_DURATION_OVERFLOW_THD (60*60*1000*90)

typedef struct{
	char *name;
	FILE *fp;
	uint64_t  tid;
	uint32_t  width;
	uint32_t  height;
	uint32_t  fps;
	uint32_t  codec;
	uint32_t  timestamp_mode;
	uint32_t  seek_mode;
	uint64_t  seek_time;
	uint64_t  duration_time;
	uint64_t  start_time;
	uint64_t  end_time;
	uint8_t   *pcache;
	uint32_t  cache_size;
	uint32_t  cache_data_len;
	uint32_t  cache_pos;

	uint8_t   psm_stream_video_type;
	uint8_t   psm_stream_audio_type;
	uint8_t   psm_stream_video_id;
	uint8_t   psm_stream_audio_id;
	uint8_t   *vidRawbuff;
	uint32_t  vidRawbuffsize;
	uint32_t  vidRawbuffpos;

	uint64_t accumulateTime;
	uint64_t curTimestamp;   //PES当前包时间戳
	uint64_t lastTimestamp;  //PES上一包时间戳
	uint64_t videoTimestamp; //修正的当前视频帧时间戳
	uint64_t corrTimestamp;	 //帧时间累积

	uint8_t  isCurKey;       //当前帧是否为I帧
	
}hkps_demux_ctx_s;

//ISO13818协议定义区
#define PS_HEADER_SCODE          (0xba)
#define PS_SYSTEM_HEADER_SCODE   (0xbb)
#define PS_PSM_HEADER_SCODE      (0xbc)

#define PS_PLAYLOAD_H264    0x1b
#define PS_PLAYLOAD_H265    0x24

typedef struct{
	uint32_t pack_start_code;
	uint8_t  _data[9];
	uint8_t  reserved:5;
	uint8_t  stuffing_len:3;
}__attribute__((packed))ps_header_s;

typedef struct{
	uint32_t header_start_code;
	uint16_t header_len;
	uint8_t  _data[6];
	uint8_t  reserved;
	//uint8_t stream_id;
	//uint8_t _data[2];
}__attribute__((packed))ps_sys_header_s;

typedef struct{
	uint8_t  packet_start_code[3];
	uint8_t  map_stream_id;
	uint16_t psm_len;
	//...
}__attribute__((packed))program_stream_map_s;

#ifdef __cplusplus
}
#endif

#endif

