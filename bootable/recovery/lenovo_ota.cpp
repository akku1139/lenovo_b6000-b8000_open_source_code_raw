#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "roots.h"

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */


//static const char *LENOVO_OTA_RESULT_FILE ="/data/data/com.lenovo.ota/files/updateResult";
//static const char *LENOVO_OTA_RESULT_FILE_NEW ="/data/ota/updateResult";
static const char *LENOVO_OTA_RESULT_FILE ="/data/ota/updateResult";

static const char * LENOVO_FACTORY_OTA_FILE_NAME = "easyimage.zip";
static const char * LENOVO_FACTORY_SD_OTA_RESULT_FILE = "/cache/recovery/factory_ota_result";

static bool remove_mota_file(const char *file_name)
{
    int  ret = 0;

    ret = unlink(file_name);

    if (ret == 0)
		return true;

	if (ret < 0 && errno == ENOENT)
		return true;

    return false;
}

static void write_result_file(const char *file_name, int result)
{
    char  dir_name[256];

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    *p = 0;

    //[MIDHNJ75] [miaotao1] 2012-11-14 fix file permission issue.
    mode_t preUmask = umask(0000);
    chmod(dir_name,0777);
    chmod(file_name,0666);
    //End, [MIDHNJ75]


    fprintf(stdout, "dir_name = %s\n", dir_name);

    if (opendir(dir_name) == NULL)  {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0777))  {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            //[MIDHNJ75] [miaotao1] 2012-11-14 fix file permission issue.
            umask(preUmask);
            //End, [MIDHNJ75]
            return;
        }
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0666);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        //[MIDHNJ75] [miaotao1] 2012-11-14 fix file permission issue.
        umask(preUmask);
        //End, [MIDHNJ75]
        return;
    }

    char tmp[12]={0x0};
    sprintf(tmp,"%d",result);
    write(result_fd, tmp, sizeof(tmp));
   
    close(result_fd);  
    //Start, [MIDHNJ75] [miaotao1] 2012-11-14 fix file permission issue.
    umask(preUmask);
    //End, [MIDHNJ75]
}



static int EndsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}


static int isFactorySdOTA(const char *update_package)
{
	return EndsWith(update_package, LENOVO_FACTORY_OTA_FILE_NAME);
}

void lenovo_ota_remove_package_and_write_result(const char *update_package, const int status)
{
    fprintf(stdout, "miaotao1 log: write result : lenovo_ota_remove_package_and_write_result\n");
    
    if(isFactorySdOTA(update_package))
    {
    	fprintf(stdout, "miaotao1 log: write result : lenovo_ota_remove_package_and_write_result it's factory sd ota\n");
    	write_result_file(LENOVO_FACTORY_SD_OTA_RESULT_FILE,status);
    	//For factory sd ota, don't delete the update package, it will be used again on another device
    }else{
	    write_result_file(LENOVO_OTA_RESULT_FILE, status);
	    
	    fprintf(stdout, "lenovo_ota_remove_package_and_write_result : remove_mota_file\n");
	    if (update_package)
	        remove_mota_file(update_package);
	}
}



static int fileExists(const char * file)
{
	if(!file)
		return 0;
	if(access(file,F_OK)==0)
	{
		fprintf(stdout, "File exsists %s\n", file);
		return 1;
	}else
	{
		fprintf(stdout, "File NOT exsists %s\n", file);
		return 0;
	}
	
}


const char * lenovo_ota_fix_patch_location(const char * update_package)
{
	if(!update_package){
		return update_package;
	}
	
	//if update_package exsit
	if(fileExists(update_package)){
		return update_package;
	}
	
	int len = strlen(update_package) + 32;
	char* modified_path = (char*)malloc(len*sizeof(char));
	memset(modified_path,0,len*sizeof(char));
	
	if(strncmp(update_package, "/sdcard", 7) == 0){
		
		//If it start with /sdcard0, /sdcard1, /sdcard2...
		int skip = 0;
		char * p = strchr(update_package+1, '/');
		if( p==NULL ){
			return update_package;
		}
		skip = p-update_package;
		
		
		//check if /sdcard0/xxxx exsit
		ensure_path_mounted("/sdcard0");
		strncpy(modified_path, "/sdcard0", len);
		strncat(modified_path, update_package+skip, len);
		if(fileExists(modified_path)){
			return modified_path;
		}
	
		//check if /sdcard1/xxxx exsit
		ensure_path_mounted("/sdcard1");
		strncpy(modified_path, "/sdcard1", len);
		strncat(modified_path, update_package+skip, len);
		if(fileExists(modified_path)){
			return modified_path;
		}
	
		//check if /sdcard2/xxxx exsit
		ensure_path_mounted("/sdcard2");
		strncpy(modified_path, "/sdcard2", len);
		strncat(modified_path, update_package+skip, len);
		if(fileExists(modified_path)){
			return modified_path;
		}
#if defined(MTK_SHARED_SDCARD)
		ensure_path_mounted("/data");
		strncpy(modified_path, "/data/media/0", len);
		strncat(modified_path, update_package+skip, len);
		if(fileExists(modified_path)){
			return modified_path;
		}
#endif
	}
	
	
	free(modified_path);
	return update_package;
}


#ifdef __cplusplus
}
#endif	/* __cplusplus */

