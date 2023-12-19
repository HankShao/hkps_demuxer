#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "hk_ps_demux.h"


int main(int argc, char *argv[])
{
	Handle hPs = HK_PS_Open(argv[1]);
	uint8_t *pdata;
	int  len;
	FILE *rawfp = fopen(argv[2], "w");
	
	if (rawfp == NULL || hPs == NULL)
	{
		printf("HK_PS_Open error!\n");
		return -1;
	}

	hkps_file_info_s info;
	HK_PS_GetFileInfo(hPs, &info);
	HK_PS_SeekPos(hPs, info.start_time+60000, 0);
	while(1)
	{
		if (HK_PS_SOK != HK_PS_ReadFrame(hPs, (void **)&pdata, &len))
			break;	

		fwrite(pdata, 1, len, rawfp);

		HK_PS_ReleaseFrame(hPs, pdata);
	}

	fclose(rawfp);
	HK_PS_Close(hPs);

	return 0;
}






