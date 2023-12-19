#ifndef __HK_PS_H__
#define __HK_PS_H__

#ifdef __cplusplus
extern "C" {
#endif 

#define Handle void *

typedef enum
{
    HK_PS_SOK = 0,
    HK_PS_EFAIL = -1,
	HK_PS_EOF = -2
} hkps_error_e;

typedef enum
{
    PS_STREAMID_VIDEO = 0,
    PS_STREAMID_AUDIO,
	PS_STREAMID_BUTT
} hkps_stream_id_e;


typedef struct hkps_stream_info
{
	int 	enType;
	int		codecType;     				 			 
    union
    {
        struct
        {
            unsigned int  width;                      
            unsigned int  height;                     
            unsigned int  fps;                        
        } video;
        struct
        {
            int      	  sampleRate;                 
            int      	  channels;                       
            int      	  bitrate;                    
            int      	  sampleDepth;                
        } audio;
    }info;	
	int				   reserved[4];
}hkps_stream_info_s;

typedef struct hkps_file_info
{
	hkps_stream_info_s  streams[PS_STREAMID_BUTT];
	long 			   start_time;//单位ms 
	long 	   		   end_time;  //单位ms
	int				   reserved[4];
}hkps_file_info_s;

/**
* @brief: 初始化PS解封装上下文
*
* @filename[in]: PS文件
*
* @return: PS解封装句柄
*
* @note.返回NULL为失败
**/
Handle HK_PS_Open(char *file);

/**
* @brief: 销毁PS解封装上下文
*
* @Handle[in]: PS句柄
*
* @return: succes 0 / fail -1
*
* @note.
**/
int HK_PS_Close(Handle hdl);

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
int HK_PS_GetFileInfo(Handle hdl, hkps_file_info_s *fileinfo);


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
int HK_PS_ReadFrame(Handle hdl,void **frmdata, int *frmlen);


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
int HK_PS_ReleaseFrame(Handle hdl,void *frmdata);


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
int HK_PS_SeekPos(Handle hdl, long pos, int mode);

#ifdef __cplusplus
}
#endif 

#endif

