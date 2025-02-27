/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <selinux/selinux.h>
#include <ftw.h>
#include <sys/capability.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <inttypes.h>

#include "cutils/misc.h"
#include "cutils/properties.h"
#include "edify/expr.h"
#include "mincrypt/sha.h"
#include "minzip/DirUtil.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "updater.h"
#include "applypatch/applypatch.h"

#include <time.h>
#include "libubi.h"
#include "ubiutils-common.h"
#include "util.h"
#include "roots.h"


#ifdef USE_EXT4
#include "make_ext4fs.h"
#endif

#if 1 //wschen 2012-05-23
#define NAND_TYPE    0
#define EMMC_TYPE    1

#define CACHE_INDEX  0
#define DATA_INDEX   1
#define SYSTEM_INDEX 2
#define CUSTOM_INDEX 3
#define FAC_INDEX 4
#endif

#ifdef MTK_SYS_FW_UPGRADE
extern Value* RetouchBinariesFnExt(const char* name, State* state, int argc, Expr* argv[]);
extern Value* UndoRetouchBinariesFnExt(const char* name, State* state, int argc, Expr* argv[]);
extern Value* ApplyDataAppsFn(const char* name, State* state, int argc, Expr* argv[]);
#endif

#if defined(CACHE_MERGE_SUPPORT)
#include <dirent.h>

static const char *DATA_CACHE_ROOT = "/data/.cache";
static int need_clear_cache = 0; 

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static int remove_dir(const char *dirname)
{
    DIR *dir;
    struct dirent *entry;
    char path[PATH_MAX];

    dir = opendir(dirname);
    if (dir == NULL) {
        LOGE("opendir %s failed\n", dirname);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            snprintf(path, (size_t) PATH_MAX, "%s/%s", dirname, entry->d_name);
            if (entry->d_type == DT_DIR) {
                remove_dir(path);
            }
            else {
                // delete file
                unlink(path);
            }
        }
    }
    closedir(dir);

    // now we can delete the empty dir
    rmdir(dirname);
    return 0;
}
#endif

// mount(fs_type, partition_type, location, mount_point)
//
//    fs_type="yaffs2" partition_type="MTD"     location=partition
//    fs_type="ext4"   partition_type="EMMC"    location=device
Value* MountFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 4) {
        return ErrorAbort(state, "%s() expects 4 args, got %d", name, argc);
    }
    char* fs_type;
    char* partition_type;
    char* location;
    char* mount_point;

#if 1 //wschen 2012-05-23
    FILE *dum;
    char buf[512];
    char p_name[32], p_size[32], p_addr[32], p_actname[64];
    unsigned int p_type;
    char dev[5][64];
    //0 -> NAND; 1 -> EMMC
    int phone_type = NAND_TYPE;

    dum = fopen("/proc/dumchar_info", "r");
    if (dum) {
        if (fgets(buf, sizeof(buf), dum) != NULL) {
            while (fgets(buf, sizeof(buf), dum)) {
                if (sscanf(buf, "%s %s %s %d %s", p_name, p_size, p_addr, &p_type, p_actname) == 5) {
                    if (!strcmp(p_name, "bmtpool")) {
                        break;
                    }
                    if (!strcmp(p_name, "preloader")) {
                        if (p_type == 2) {
                            phone_type = EMMC_TYPE;
                        }
                    } else if (!strcmp(p_name, "cache")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[CACHE_INDEX], sizeof(dev[CACHE_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[CACHE_INDEX], sizeof(dev[CACHE_INDEX]), "cache");
                        }
                    } else if (!strcmp(p_name, "usrdata")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[DATA_INDEX], sizeof(dev[DATA_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[DATA_INDEX], sizeof(dev[DATA_INDEX]), "userdata");
                        }
                    } else if (!strcmp(p_name, "android")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[SYSTEM_INDEX], sizeof(dev[SYSTEM_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[SYSTEM_INDEX], sizeof(dev[SYSTEM_INDEX]), "system");
                        }
                    } else if (!strcmp(p_name, "custom")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[CUSTOM_INDEX], sizeof(dev[CUSTOM_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[CUSTOM_INDEX], sizeof(dev[CUSTOM_INDEX]), "custom");
                        }
                    } else if (!strcmp(p_name, "fac")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[FAC_INDEX], sizeof(dev[FAC_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[FAC_INDEX], sizeof(dev[FAC_INDEX]), "fac");
                        }
                    }
                }
            }
        }
        fclose(dum);
    }
#endif

    if (ReadArgs(state, argv, 4, &fs_type, &partition_type,
                 &location, &mount_point) < 0) {
        return NULL;
    }

    if (strlen(fs_type) == 0) {
        ErrorAbort(state, "fs_type argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(partition_type) == 0) {
        ErrorAbort(state, "partition_type argument to %s() can't be empty",
                   name);
        goto done;
    }
    if (strlen(location) == 0) {
        ErrorAbort(state, "location argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to %s() can't be empty", name);
        goto done;
    }

#if defined(CACHE_MERGE_SUPPORT)
    if (!strcmp(mount_point, "/cache")) {
        const MountedVolume* vol;

        scan_mounted_volumes();
        vol = find_mounted_volume_by_mount_point("/data");
        if (vol) {
            // create link if data is already mounted
            if (symlink(DATA_CACHE_ROOT, "/cache")) {
                if (errno != EEXIST) {
                    fprintf(stderr, "create symlink from %s to %s failed(%s)\n", 
                                        DATA_CACHE_ROOT, "/cache", strerror(errno));
                    result = strdup("");
                    goto done;
                }
            }
            result = strdup(mount_point);
            goto done;
        } else {
            // cache is under /data now, mount it
            mount_point = "/data";
        }
    }
#endif

    char *secontext = NULL;

    if (sehandle) {
        selabel_lookup(sehandle, &secontext, mount_point, 0755);
        setfscreatecon(secontext);
    }

    mkdir(mount_point, 0755);

    if (secontext) {
        freecon(secontext);
        setfscreatecon(NULL);
    }

#if 0 //wschen 2012-07-10
    if (strcmp(partition_type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd;
        mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_mount_partition(mtd, mount_point, fs_type, 0 /* rw */) != 0) {
            fprintf(stderr, "mtd mount of %s failed: %s\n",
                    location, strerror(errno));
            result = strdup("");
            goto done;
        }
        result = mount_point;
    } else {
        if (mount(location, mount_point, fs_type,
                  MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
            fprintf(stderr, "%s: failed to mount %s at %s: %s\n",
                    name, location, mount_point, strerror(errno));
            result = strdup("");
        } else {
            result = mount_point;
        }
    }
#else
    if (!strcmp(mount_point, "/system") || !strcmp(mount_point, "/data") || !strcmp(mount_point, "/cache") || !strcmp(mount_point, "/custom") || !strcmp(mount_point, "/fac")) {

        if (!strcmp(mount_point, "/system")) {
            free(location);
            location = strdup(dev[SYSTEM_INDEX]);
            fprintf(stderr, "mount %s %s\n", mount_point, location);
        } else if (!strcmp(mount_point, "/data")) {
            free(location);
            location = strdup(dev[DATA_INDEX]);
            fprintf(stderr, "mount %s %s\n", mount_point, location);
        } else if (!strcmp(mount_point, "/cache")) {
            free(location);
            location = strdup(dev[CACHE_INDEX]);
            fprintf(stderr, "mount %s %s\n", mount_point, location);
        } else if (!strcmp(mount_point, "/custom")) {
            free(location);
            location = strdup(dev[CUSTOM_INDEX]);
            fprintf(stderr, "mount %s %s\n", mount_point, location);
        } else if (!strcmp(mount_point, "/fac")) {
            free(location);
            location = strdup(dev[FAC_INDEX]);
            fprintf(stderr, "mount %s %s\n", mount_point, location);
        }


        if (phone_type == NAND_TYPE) {

#if defined(UBIFS_SUPPORT)	
				//Attatch UBI device & Make UBI volum
				int n=-1;
				int ret;
				n = ubi_attach_mtd_user(mount_point);
			
				if((n!=-1) && (n<4)){
					//attached successful, do nothing
				}	
				else{
					ErrorAbort(state, "failed to attach %s\n", location);
        			goto done;
				}
					
				//Mount UBI volume
				const unsigned long flags = MS_NOATIME | MS_NODEV | MS_NODIRATIME;
				char tmp[64];
				sprintf(tmp, "/dev/ubi%d_0", n);
			
				ret = mount(tmp, mount_point, fs_type, flags, "");
				if ( ret < 0) {
					ubi_detach_dev(n);
					ErrorAbort(state, "failed to mount %s\n", mount_point);
					result = strdup("");
					goto done;
				}
				else if (ret == 0) 
					result = mount_point;
				//Volume  successfully	mounted
				fprintf(stderr, "UBI mount successful %s\n");
#else
			
            free(fs_type);
            fs_type = strdup("yaffs2");
            free(partition_type);
            partition_type = strdup("MTD");

            mtd_scan_partitions();
            const MtdPartition* mtd;
            mtd = mtd_find_partition_by_name(location);
            if (mtd == NULL) {
                fprintf(stderr, "%s: no mtd partition named \"%s\"",
                        name, location);
                result = strdup("");
                goto done;
            }
            if (mtd_mount_partition(mtd, mount_point, fs_type, 0 /* rw */) != 0) {
                fprintf(stderr, "mtd mount of %s failed: %s\n",
                        location, strerror(errno));
                result = strdup("");
                goto done;
            }
            result = mount_point;
#endif

        } else {
            free(fs_type);
            fs_type = strdup("ext4");
            free(partition_type);
            partition_type = strdup("EMMC");

            if (mount(location, mount_point, fs_type,
                        MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
                fprintf(stderr, "%s: failed to mount %s at %s: %s\n",
                        name, location, mount_point, strerror(errno));
                result = strdup("");
            } else {
                result = mount_point;
            }
        }

    } else {

    if (strcmp(partition_type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd;
        mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_mount_partition(mtd, mount_point, fs_type, 0 /* rw */) != 0) {
            fprintf(stderr, "mtd mount of %s failed: %s\n",
                    location, strerror(errno));
            result = strdup("");
            goto done;
        }
        result = mount_point;
    } else {
        if (mount(location, mount_point, fs_type,
                  MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
            fprintf(stderr, "%s: failed to mount %s at %s: %s\n",
                    name, location, mount_point, strerror(errno));
            result = strdup("");
        } else {
            result = mount_point;
        }
    }

    }
#endif

done:
#if defined(CACHE_MERGE_SUPPORT)
    if (!strcmp(mount_point, "/data")) {
        if (mkdir(DATA_CACHE_ROOT, 0770)) {
            if (errno != EEXIST) {
                fprintf(stderr, "mkdir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                result = strdup("");
            } else if (need_clear_cache) {
                fprintf(stderr, "cache exists, clear it...\n");
                if (remove_dir(DATA_CACHE_ROOT)) {
                    fprintf(stderr, "remove_dir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                    result = strdup("");
                }
                if (mkdir(DATA_CACHE_ROOT, 0770) != 0) {
                    fprintf(stderr, "Can't mkdir %s (%s)\n", DATA_CACHE_ROOT, strerror(errno));
                    result = strdup("");
                }                
            }
        }
        if (symlink(DATA_CACHE_ROOT, "/cache")) {
            if (errno != EEXIST) {
                fprintf(stderr, "create symlink from %s to %s failed(%s)\n", 
                                DATA_CACHE_ROOT, "/cache", strerror(errno));
                result = strdup("");
            }
        }
        need_clear_cache = 0;
    }
#endif

    free(fs_type);
    free(partition_type);
    free(location);
    if (result != mount_point) free(mount_point);
    return StringValue(result);
}


// is_mounted(mount_point)
Value* IsMountedFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* mount_point;
    if (ReadArgs(state, argv, 1, &mount_point) < 0) {
        return NULL;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to unmount() can't be empty");
        goto done;
    }

    scan_mounted_volumes();
    const MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point);
    if (vol == NULL) {
        result = strdup("");
    } else {
        result = mount_point;
    }

done:
    if (result != mount_point) free(mount_point);
    return StringValue(result);
}


Value* UnmountFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    int ubi_num;
	
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* mount_point;
    if (ReadArgs(state, argv, 1, &mount_point) < 0) {
        return NULL;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to unmount() can't be empty");
        goto done;
    }

#if defined(CACHE_MERGE_SUPPORT)
    if (!strcmp(mount_point, "/cache")) {
        // remove cache link
        unlink(mount_point);
        return mount_point;            
    }
#endif

    scan_mounted_volumes();
    const MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point);
    if (vol == NULL) {
        fprintf(stderr, "unmount of %s failed; no such volume\n", mount_point);
        result = strdup("");
    } else {
        unmount_mounted_volume(vol);
        result = mount_point;
    }

#if defined(UBIFS_SUPPORT)
	if (!(!strcmp(mount_point, "/system")||!strcmp(mount_point, "/data")||!strcmp(mount_point, "/cache")||!strcmp(mount_point, "/.cache")||!strcmp(mount_point, "/custom"))){
		LOGE("Invalid mount_point: %s\n", mount_point);
		return -1;
	}

	if (!strcmp(mount_point, "/system")){
		ubi_num = 0;
	}
	
	if (!strcmp(mount_point, "/data")){
		ubi_num = 1;
	}
	
	if (!strcmp(mount_point, "/cache")||!strcmp(mount_point, "/.cache")){
		ubi_num = 2;
	}

	if (!strcmp(mount_point, "/custom")){
		//ubi_num = 3;
	}
	fprintf(stderr, "detaching ubi%d\n", ubi_num);
	if(ubi_detach_dev(ubi_num)==-1){
		fprintf(stderr, "detaching ubi%d failed\n", ubi_num);
	}
#endif
	
done:
    if (result != mount_point) free(mount_point);
    return StringValue(result);
}


// format(fs_type, partition_type, location, fs_size, mount_point)
//
//    fs_type="yaffs2" partition_type="MTD"     location=partition fs_size=<bytes> mount_point=<location>
//    fs_type="ext4"   partition_type="EMMC"    location=device    fs_size=<bytes> mount_point=<location>
//    if fs_size == 0, then make_ext4fs uses the entire partition.
//    if fs_size > 0, that is the size to use
//    if fs_size < 0, then reserve that many bytes at the end of the partition
Value* FormatFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 5) {
        return ErrorAbort(state, "%s() expects 5 args, got %d", name, argc);
    }
    char* fs_type;
    char* partition_type;
    char* location;
    char* fs_size;
    char* mount_point;

    if (ReadArgs(state, argv, 5, &fs_type, &partition_type, &location, &fs_size, &mount_point) < 0) {
        return NULL;
    }

#if 1 //wschen 2012-07-10
    FILE *dum;
    char buf[512];
    char p_name[32], p_size[32], p_addr[32], p_actname[64];
    char dev[5][64];
    unsigned int p_type;
    //0 -> NAND; 1 -> EMMC
    int phone_type = NAND_TYPE;

    dum = fopen("/proc/dumchar_info", "r");
    if (dum) {
        if (fgets(buf, sizeof(buf), dum) != NULL) {
            while (fgets(buf, sizeof(buf), dum)) {
                if (sscanf(buf, "%s %s %s %d %s", p_name, p_size, p_addr, &p_type, p_actname) == 5) {
                    if (!strcmp(p_name, "bmtpool")) {
                        break;
                    }
                    if (!strcmp(p_name, "preloader")) {
                        if (p_type == 2) {
                            phone_type = EMMC_TYPE;
                        }
                    } else if (!strcmp(p_name, "cache")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[CACHE_INDEX], sizeof(dev[CACHE_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[CACHE_INDEX], sizeof(dev[CACHE_INDEX]), "cache");
                        }
                    } else if (!strcmp(p_name, "usrdata")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[DATA_INDEX], sizeof(dev[DATA_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[DATA_INDEX], sizeof(dev[DATA_INDEX]), "userdata");
                        }
                    } else if (!strcmp(p_name, "android")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[SYSTEM_INDEX], sizeof(dev[SYSTEM_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[SYSTEM_INDEX], sizeof(dev[SYSTEM_INDEX]), "system");
                        }
                    } else if (!strcmp(p_name, "custom")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[CUSTOM_INDEX], sizeof(dev[CUSTOM_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[CUSTOM_INDEX], sizeof(dev[CUSTOM_INDEX]), "custom");
                        }
                    } else if (!strcmp(p_name, "fac")) {
                        if (phone_type == EMMC_TYPE) {
                            snprintf(dev[FAC_INDEX], sizeof(dev[FAC_INDEX]), "%s", p_actname);
                        } else {
                            snprintf(dev[FAC_INDEX], sizeof(dev[FAC_INDEX]), "fac");
                        }
                    }
                }
            }
        }
        fclose(dum);
    }
#endif

    if (strlen(fs_type) == 0) {
        ErrorAbort(state, "fs_type argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(partition_type) == 0) {
        ErrorAbort(state, "partition_type argument to %s() can't be empty",
                   name);
        goto done;
    }
    if (strlen(location) == 0) {
        ErrorAbort(state, "location argument to %s() can't be empty", name);
        goto done;
    }

    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to %s() can't be empty", name);
        goto done;
    }

#if 0 //wschen 2012-07-10
    if (strcmp(partition_type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        MtdWriteContext* ctx = mtd_write_partition(mtd);
        if (ctx == NULL) {
            fprintf(stderr, "%s: can't write \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_erase_blocks(ctx, -1) == (off64_t)-1) {
            mtd_write_close(ctx);
            fprintf(stderr, "%s: failed to erase \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_write_close(ctx) != 0) {
            fprintf(stderr, "%s: failed to close \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        result = location;
#ifdef USE_EXT4
    } else if (strcmp(fs_type, "ext4") == 0) {
        int status = make_ext4fs(location, atoll(fs_size), mount_point, sehandle);
        if (status != 0) {
            fprintf(stderr, "%s: make_ext4fs failed (%d) on %s",
                    name, status, location);
            result = strdup("");
            goto done;
        }
        result = location;
#endif
    } else {
        fprintf(stderr, "%s: unsupported fs_type \"%s\" partition_type \"%s\"",
                name, fs_type, partition_type);
    }
#else
    if (!strcmp(mount_point, "/system") || !strcmp(mount_point, "/data") || !strcmp(mount_point, "/cache") || !strcmp(mount_point, "/custom") || !strcmp(mount_point, "/fac")) {
#if defined(CACHE_MERGE_SUPPORT)
        if (!strcmp(mount_point, "/cache")) {
            const MountedVolume* vol;

            // set flag if /data is already unmounted
            // cache will be cleared after data mounted
            scan_mounted_volumes();
            vol = find_mounted_volume_by_mount_point("/data");
            if (vol == NULL) {
                fprintf(stderr, "/data is unmounted before formatting cache!\n");
                need_clear_cache = 1;
            } else {
                if (remove_dir(DATA_CACHE_ROOT)) {
                    fprintf(stderr, "remove_dir %s error: %s\n", DATA_CACHE_ROOT, strerror(errno));
                    result = strdup("");
                    goto done;
                }
                if (mkdir(DATA_CACHE_ROOT, 0770) != 0) {
                    fprintf(stderr, "Can't mkdir %s (%s)\n", DATA_CACHE_ROOT, strerror(errno));
                    result = strdup("");
                    goto done;
                }
                fprintf(stderr, "format cache successfully!\n");
            }
            result = strdup(dev[CACHE_INDEX]);
            goto done;
        }
#endif
        if (!strcmp(mount_point, "/system")) {
            free(location);
            location = strdup(dev[SYSTEM_INDEX]);
            fprintf(stderr, "format /system %s\n", location);
        } else if (!strcmp(mount_point, "/data")) {
            free(location);
            location = strdup(dev[DATA_INDEX]);
            fprintf(stderr, "format /data %s\n", location);
        } else if (!strcmp(mount_point, "/cache")) {
            free(location);
            location = strdup(dev[CACHE_INDEX]);
            fprintf(stderr, "format /cache %s\n", location);
        } else if (!strcmp(mount_point, "/custom")) {
            free(location);
            location = strdup(dev[CUSTOM_INDEX]);
            fprintf(stderr, "format /custom %s\n", location);
        } else if (!strcmp(mount_point, "/fac")) {
            free(location);
            location = strdup(dev[FAC_INDEX]);
            fprintf(stderr, "format /FAC %s\n", location);
        }

        if (phone_type == NAND_TYPE) {
#if defined(UBIFS_SUPPORT)	
		int ret;
		ret = ubi_format(mount_point);
				
		if(ret!=0){
			fprintf(stderr, "%s: no mtd partition named \"%s\"", name, location);
                        result = strdup("");
                        goto done;
		}
		else{
			result = location;
		}

#else
            mtd_scan_partitions();
            const MtdPartition* mtd = mtd_find_partition_by_name(location);
            if (mtd == NULL) {
                fprintf(stderr, "%s: no mtd partition named \"%s\"", name, location);
                result = strdup("");
                goto done;
            }
            MtdWriteContext* ctx = mtd_write_partition(mtd);
            if (ctx == NULL) {
                fprintf(stderr, "%s: can't write \"%s\"", name, location);
                result = strdup("");
                goto done;
            }
            if (mtd_erase_blocks(ctx, -1) == (off64_t)-1) {
                mtd_write_close(ctx);
                fprintf(stderr, "%s: failed to erase \"%s\"", name, location);
                result = strdup("");
                goto done;
            }
            if (mtd_write_close(ctx) != 0) {
                fprintf(stderr, "%s: failed to close \"%s\"", name, location);
                result = strdup("");
                goto done;
            }
            result = location;
#endif
        }

#ifdef USE_EXT4
          else {
            int status = make_ext4fs(location, atoll(fs_size), mount_point, sehandle);
            if (status != 0) {
                fprintf(stderr, "%s: make_ext4fs failed (%d) on %s",
                        name, status, location);
                result = strdup("");
                goto done;
            }
            result = location;
        }
#endif

    } else {

    if (strcmp(partition_type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        MtdWriteContext* ctx = mtd_write_partition(mtd);
        if (ctx == NULL) {
            fprintf(stderr, "%s: can't write \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
            if (mtd_erase_blocks(ctx, -1) == (off64_t)-1) {
            mtd_write_close(ctx);
            fprintf(stderr, "%s: failed to erase \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_write_close(ctx) != 0) {
            fprintf(stderr, "%s: failed to close \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        result = location;
#ifdef USE_EXT4
    } else if (strcmp(fs_type, "ext4") == 0) {
        int status = make_ext4fs(location, atoll(fs_size), mount_point, sehandle);
        if (status != 0) {
            fprintf(stderr, "%s: make_ext4fs failed (%d) on %s",
                    name, status, location);
            result = strdup("");
            goto done;
        }
        result = location;
#endif
    } else {
        fprintf(stderr, "%s: unsupported fs_type \"%s\" partition_type \"%s\"",
                name, fs_type, partition_type);
    }
    }
#endif

done:
    free(fs_type);
    free(partition_type);
#if 1 //wschen 2012-07-11
    free(mount_point);
#endif
    if (result != location) free(location);
    return StringValue(result);
}

Value* RenameFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }

    char* src_name;
    char* dst_name;

    if (ReadArgs(state, argv, 2, &src_name, &dst_name) < 0) {
        return NULL;
    }
    if (strlen(src_name) == 0) {
        ErrorAbort(state, "src_name argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(dst_name) == 0) {
        ErrorAbort(state, "dst_name argument to %s() can't be empty",
                   name);
        goto done;
    }

    if (rename(src_name, dst_name) != 0) {
        ErrorAbort(state, "Rename of %s() to %s() failed, error %s()",
          src_name, dst_name, strerror(errno));
    } else {
        result = dst_name;
    }

done:
    free(src_name);
    if (result != dst_name) free(dst_name);
    return StringValue(result);
}

Value* DeleteFn(const char* name, State* state, int argc, Expr* argv[]) {
    char** paths = malloc(argc * sizeof(char*));
    int i;
    for (i = 0; i < argc; ++i) {
        paths[i] = Evaluate(state, argv[i]);
        if (paths[i] == NULL) {
            int j;
            for (j = 0; j < i; ++i) {
                free(paths[j]);
            }
            free(paths);
            return NULL;
        }
    }

    bool recursive = (strcmp(name, "delete_recursive") == 0);

    int success = 0;
    for (i = 0; i < argc; ++i) {
        if ((recursive ? dirUnlinkHierarchy(paths[i]) : unlink(paths[i])) == 0)
            ++success;
        free(paths[i]);
    }
    free(paths);

    char buffer[10];
    sprintf(buffer, "%d", success);
    return StringValue(strdup(buffer));
}


Value* ShowProgressFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* frac_str;
    char* sec_str;
    if (ReadArgs(state, argv, 2, &frac_str, &sec_str) < 0) {
        return NULL;
    }

    double frac = strtod(frac_str, NULL);
    int sec = strtol(sec_str, NULL, 10);

    UpdaterInfo* ui = (UpdaterInfo*)(state->cookie);
    fprintf(ui->cmd_pipe, "progress %f %d\n", frac, sec);

    free(sec_str);
    return StringValue(frac_str);
}

Value* SetProgressFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* frac_str;
    if (ReadArgs(state, argv, 1, &frac_str) < 0) {
        return NULL;
    }

    double frac = strtod(frac_str, NULL);

    UpdaterInfo* ui = (UpdaterInfo*)(state->cookie);
    fprintf(ui->cmd_pipe, "set_progress %f\n", frac);

    return StringValue(frac_str);
}

// package_extract_dir(package_path, destination_path)
Value* PackageExtractDirFn(const char* name, State* state,
                          int argc, Expr* argv[]) {
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* zip_path;
    char* dest_path;
    if (ReadArgs(state, argv, 2, &zip_path, &dest_path) < 0) return NULL;

    ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;

    // To create a consistent system image, never use the clock for timestamps.
    struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default

    bool success = mzExtractRecursive(za, zip_path, dest_path,
                                      MZ_EXTRACT_FILES_ONLY, &timestamp,
                                      NULL, NULL, sehandle);
    free(zip_path);
    free(dest_path);
    return StringValue(strdup(success ? "t" : ""));
}


// package_extract_file(package_path, destination_path)
//   or
// package_extract_file(package_path)
//   to return the entire contents of the file as the result of this
//   function (the char* returned is actually a FileContents*).
Value* PackageExtractFileFn(const char* name, State* state,
                           int argc, Expr* argv[]) {
    if (argc != 1 && argc != 2) {
        return ErrorAbort(state, "%s() expects 1 or 2 args, got %d",
                          name, argc);
    }
    bool success = false;
    if (argc == 2) {
        // The two-argument version extracts to a file.

        char* zip_path;
        char* dest_path;
        if (ReadArgs(state, argv, 2, &zip_path, &dest_path) < 0) return NULL;

        ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;
        const ZipEntry* entry = mzFindZipEntry(za, zip_path);
        if (entry == NULL) {
            fprintf(stderr, "%s: no %s in package\n", name, zip_path);
            goto done2;
        }

#if defined(UBIFS_SUPPORT)
	if(!strcmp(dest_path, "system.img")){
		const MtdPartition *partition;
		const char* partition_name;
		char dev_name[20];
		int32_t mtd_num;
		partition_name = strdup("system");
		
		mtd_scan_partitions();
		partition = mtd_find_partition_by_name(partition_name);
		if (partition == NULL) {
			printf("failed to find \"%s\" partition at /dev/mtd\n",
			partition_name);
            goto done2;
		}
		//Get mtd_dev_name 
		mtd_num = mtd_part_to_number(partition);
		sprintf(dev_name, "/dev/mtd/mtd%d", mtd_num);
		printf("dev_name = %s\n", dev_name);
		
		//Erase 
		MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            printf("format_volume: can't open MTD \"%s\"\n", dev_name);
            goto done2;
        } else if (mtd_erase_blocks(write, -1) == (off64_t) -1) {
            printf("format_volume: can't erase MTD \"%s\"\n", dev_name);
            mtd_write_close(write);
            goto done2;
        } else if (mtd_write_close(write)) {
            printf("format_volume: can't close MTD \"%s\"\n", dev_name);
            goto done2;
        }
		
		//Extract and Write
		FILE* f = fopen(dev_name, "wb"); 
		if (f == NULL) {
            fprintf(stderr, "%s: can't open %s for write: %s\n",
                    name, dev_name, strerror(errno));
            goto done2;
        }
        success = mzExtractZipEntryToFile(za, entry, fileno(f));
        fclose(f);
		
		free(zip_path);
        free(dest_path);
        return StringValue(strdup(success ? "t" : ""));
	}		
#endif

        FILE* f = fopen(dest_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "%s: can't open %s for write: %s\n",
                    name, dest_path, strerror(errno));
            goto done2;
        }
        success = mzExtractZipEntryToFile(za, entry, fileno(f));
        fclose(f);

      done2:
        free(zip_path);
        free(dest_path);
        return StringValue(strdup(success ? "t" : ""));
    } else {
        // The one-argument version returns the contents of the file
        // as the result.

        char* zip_path;
        Value* v = malloc(sizeof(Value));
        v->type = VAL_BLOB;
        v->size = -1;
        v->data = NULL;

        if (ReadArgs(state, argv, 1, &zip_path) < 0) return NULL;

        ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;
        const ZipEntry* entry = mzFindZipEntry(za, zip_path);
        if (entry == NULL) {
            fprintf(stderr, "%s: no %s in package\n", name, zip_path);
            goto done1;
        }

        v->size = mzGetZipEntryUncompLen(entry);
        v->data = malloc(v->size);
        if (v->data == NULL) {
            fprintf(stderr, "%s: failed to allocate %ld bytes for %s\n",
                    name, (long)v->size, zip_path);
            goto done1;
        }

        success = mzExtractZipEntryToBuffer(za, entry,
                                            (unsigned char *)v->data);

      done1:
        free(zip_path);
        if (!success) {
            free(v->data);
            v->data = NULL;
            v->size = -1;
        }
        return v;
    }
}

// Create all parent directories of name, if necessary.
static int make_parents(char* name) {
    char* p;
    for (p = name + (strlen(name)-1); p > name; --p) {
        if (*p != '/') continue;
        *p = '\0';
        if (make_parents(name) < 0) return -1;
        int result = mkdir(name, 0700);
        if (result == 0) fprintf(stderr, "symlink(): created [%s]\n", name);
        *p = '/';
        if (result == 0 || errno == EEXIST) {
            // successfully created or already existed; we're done
            return 0;
        } else {
            fprintf(stderr, "failed to mkdir %s: %s\n", name, strerror(errno));
            return -1;
        }
    }
    return 0;
}

// symlink target src1 src2 ...
//    unlinks any previously existing src1, src2, etc before creating symlinks.
Value* SymlinkFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc == 0) {
        return ErrorAbort(state, "%s() expects 1+ args, got %d", name, argc);
    }
    char* target;
    target = Evaluate(state, argv[0]);
    if (target == NULL) return NULL;

    char** srcs = ReadVarArgs(state, argc-1, argv+1);
    if (srcs == NULL) {
        free(target);
        return NULL;
    }

    int bad = 0;
    int i;
    for (i = 0; i < argc-1; ++i) {
        if (unlink(srcs[i]) < 0) {
            if (errno != ENOENT) {
                fprintf(stderr, "%s: failed to remove %s: %s\n",
                        name, srcs[i], strerror(errno));
                ++bad;
            }
        }
        if (make_parents(srcs[i])) {
            fprintf(stderr, "%s: failed to symlink %s to %s: making parents failed\n",
                    name, srcs[i], target);
            ++bad;
        }
        if (symlink(target, srcs[i]) < 0) {
            fprintf(stderr, "%s: failed to symlink %s to %s: %s\n",
                    name, srcs[i], target, strerror(errno));
            ++bad;
        }
        free(srcs[i]);
    }
    free(srcs);
    if (bad) {
        return ErrorAbort(state, "%s: some symlinks failed", name);
    }
    return StringValue(strdup(""));
}


Value* SetPermFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    bool recursive = (strcmp(name, "set_perm_recursive") == 0);

    int min_args = 4 + (recursive ? 1 : 0);
    if (argc < min_args) {
        return ErrorAbort(state, "%s() expects %d+ args, got %d",
                          name, min_args, argc);
    }

    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) return NULL;

    char* end;
    int i;
    int bad = 0;

    int uid = strtoul(args[0], &end, 0);
    if (*end != '\0' || args[0][0] == 0) {
        ErrorAbort(state, "%s: \"%s\" not a valid uid", name, args[0]);
        goto done;
    }

    int gid = strtoul(args[1], &end, 0);
    if (*end != '\0' || args[1][0] == 0) {
        ErrorAbort(state, "%s: \"%s\" not a valid gid", name, args[1]);
        goto done;
    }

    if (recursive) {
        int dir_mode = strtoul(args[2], &end, 0);
        if (*end != '\0' || args[2][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid dirmode", name, args[2]);
            goto done;
        }

        int file_mode = strtoul(args[3], &end, 0);
        if (*end != '\0' || args[3][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid filemode",
                       name, args[3]);
            goto done;
        }

        for (i = 4; i < argc; ++i) {
            dirSetHierarchyPermissions(args[i], uid, gid, dir_mode, file_mode);
        }
    } else {
        int mode = strtoul(args[2], &end, 0);
        if (*end != '\0' || args[2][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid mode", name, args[2]);
            goto done;
        }

        for (i = 3; i < argc; ++i) {
            if (chown(args[i], uid, gid) < 0) {
                fprintf(stderr, "%s: chown of %s to %d %d failed: %s\n",
                        name, args[i], uid, gid, strerror(errno));
                ++bad;
            }
            if (chmod(args[i], mode) < 0) {
                fprintf(stderr, "%s: chmod of %s to %o failed: %s\n",
                        name, args[i], mode, strerror(errno));
                ++bad;
            }
        }
    }
    result = strdup("");

done:
    for (i = 0; i < argc; ++i) {
        free(args[i]);
    }
    free(args);

    if (bad) {
        free(result);
        return ErrorAbort(state, "%s: some changes failed", name);
    }
    return StringValue(result);
}

struct perm_parsed_args {
    bool has_uid;
    uid_t uid;
    bool has_gid;
    gid_t gid;
    bool has_mode;
    mode_t mode;
    bool has_fmode;
    mode_t fmode;
    bool has_dmode;
    mode_t dmode;
    bool has_selabel;
    char* selabel;
    bool has_capabilities;
    uint64_t capabilities;
};

static struct perm_parsed_args ParsePermArgs(int argc, char** args) {
    int i;
    struct perm_parsed_args parsed;
    int bad = 0;
    static int max_warnings = 20;

    memset(&parsed, 0, sizeof(parsed));

    for (i = 1; i < argc; i += 2) {
        if (strcmp("uid", args[i]) == 0) {
            int64_t uid;
            if (sscanf(args[i+1], "%" SCNd64, &uid) == 1) {
                parsed.uid = uid;
                parsed.has_uid = true;
            } else {
                printf("ParsePermArgs: invalid UID \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("gid", args[i]) == 0) {
            int64_t gid;
            if (sscanf(args[i+1], "%" SCNd64, &gid) == 1) {
                parsed.gid = gid;
                parsed.has_gid = true;
            } else {
                printf("ParsePermArgs: invalid GID \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("mode", args[i]) == 0) {
            int32_t mode;
            if (sscanf(args[i+1], "%" SCNi32, &mode) == 1) {
                parsed.mode = mode;
                parsed.has_mode = true;
            } else {
                printf("ParsePermArgs: invalid mode \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("dmode", args[i]) == 0) {
            int32_t mode;
            if (sscanf(args[i+1], "%" SCNi32, &mode) == 1) {
                parsed.dmode = mode;
                parsed.has_dmode = true;
            } else {
                printf("ParsePermArgs: invalid dmode \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("fmode", args[i]) == 0) {
            int32_t mode;
            if (sscanf(args[i+1], "%" SCNi32, &mode) == 1) {
                parsed.fmode = mode;
                parsed.has_fmode = true;
            } else {
                printf("ParsePermArgs: invalid fmode \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("capabilities", args[i]) == 0) {
            int64_t capabilities;
            if (sscanf(args[i+1], "%" SCNi64, &capabilities) == 1) {
                parsed.capabilities = capabilities;
                parsed.has_capabilities = true;
            } else {
                printf("ParsePermArgs: invalid capabilities \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (strcmp("selabel", args[i]) == 0) {
            if (args[i+1][0] != '\0') {
                parsed.selabel = args[i+1];
                parsed.has_selabel = true;
            } else {
                printf("ParsePermArgs: invalid selabel \"%s\"\n", args[i + 1]);
                bad++;
            }
            continue;
        }
        if (max_warnings != 0) {
            printf("ParsedPermArgs: unknown key \"%s\", ignoring\n", args[i]);
            max_warnings--;
            if (max_warnings == 0) {
                printf("ParsedPermArgs: suppressing further warnings\n");
            }
        }
    }
    return parsed;
}

static int ApplyParsedPerms(
        const char* filename,
        const struct stat *statptr,
        struct perm_parsed_args parsed)
{
    int bad = 0;

    /* ignore symlinks */
    if (S_ISLNK(statptr->st_mode)) {
        return 0;
    }

    if (parsed.has_uid) {
        if (chown(filename, parsed.uid, -1) < 0) {
            printf("ApplyParsedPerms: chown of %s to %d failed: %s\n",
                   filename, parsed.uid, strerror(errno));
            bad++;
        }
    }

    if (parsed.has_gid) {
        if (chown(filename, -1, parsed.gid) < 0) {
            printf("ApplyParsedPerms: chgrp of %s to %d failed: %s\n",
                   filename, parsed.gid, strerror(errno));
            bad++;
        }
    }

    if (parsed.has_mode) {
        if (chmod(filename, parsed.mode) < 0) {
            printf("ApplyParsedPerms: chmod of %s to %d failed: %s\n",
                   filename, parsed.mode, strerror(errno));
            bad++;
        }
    }

    if (parsed.has_dmode && S_ISDIR(statptr->st_mode)) {
        if (chmod(filename, parsed.dmode) < 0) {
            printf("ApplyParsedPerms: chmod of %s to %d failed: %s\n",
                   filename, parsed.dmode, strerror(errno));
            bad++;
        }
    }

    if (parsed.has_fmode && S_ISREG(statptr->st_mode)) {
        if (chmod(filename, parsed.fmode) < 0) {
            printf("ApplyParsedPerms: chmod of %s to %d failed: %s\n",
                   filename, parsed.fmode, strerror(errno));
            bad++;
        }
    }

    if (parsed.has_selabel) {
        // TODO: Don't silently ignore ENOTSUP
        if (lsetfilecon(filename, parsed.selabel) && (errno != ENOTSUP)) {
            printf("ApplyParsedPerms: lsetfilecon of %s to %s failed: %s\n",
                   filename, parsed.selabel, strerror(errno));
            bad++;
        }
    }

#if 0
    if (parsed.has_capabilities && S_ISREG(statptr->st_mode)) {
        if (parsed.capabilities == 0) {
            if ((removexattr(filename, XATTR_NAME_CAPS) == -1) && (errno != ENODATA)) {
                // Report failure unless it's ENODATA (attribute not set)
                printf("ApplyParsedPerms: removexattr of %s to %" PRIx64 " failed: %s\n",
                       filename, parsed.capabilities, strerror(errno));
                bad++;
            }
        } else {
            struct vfs_cap_data cap_data;
            memset(&cap_data, 0, sizeof(cap_data));
            cap_data.magic_etc = VFS_CAP_REVISION | VFS_CAP_FLAGS_EFFECTIVE;
            cap_data.data[0].permitted = (uint32_t) (parsed.capabilities & 0xffffffff);
            cap_data.data[0].inheritable = 0;
            cap_data.data[1].permitted = (uint32_t) (parsed.capabilities >> 32);
            cap_data.data[1].inheritable = 0;
            if (setxattr(filename, XATTR_NAME_CAPS, &cap_data, sizeof(cap_data), 0) < 0) {
                printf("ApplyParsedPerms: setcap of %s to %" PRIx64 " failed: %s\n",
                       filename, parsed.capabilities, strerror(errno));
                bad++;
            }
        }
    }
#endif

    return bad;
}

// nftw doesn't allow us to pass along context, so we need to use
// global variables.  *sigh*
static struct perm_parsed_args recursive_parsed_args;

static int do_SetMetadataRecursive(const char* filename, const struct stat *statptr,
        int fileflags, struct FTW *pfwt) {
    return ApplyParsedPerms(filename, statptr, recursive_parsed_args);
}

static Value* SetMetadataFn(const char* name, State* state, int argc, Expr* argv[]) {
    int i;
    int bad = 0;
    static int nwarnings = 0;
    struct stat sb;
    Value* result = NULL;

    bool recursive = (strcmp(name, "set_metadata_recursive") == 0);

    if ((argc % 2) != 1) {
        return ErrorAbort(state, "%s() expects an odd number of arguments, got %d",
                          name, argc);
    }

    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) return NULL;

    if (lstat(args[0], &sb) == -1) {
        result = ErrorAbort(state, "%s: Error on lstat of \"%s\": %s", name, args[0], strerror(errno));
        goto done;
    }

    struct perm_parsed_args parsed = ParsePermArgs(argc, args);

    if (recursive) {
        recursive_parsed_args = parsed;
        bad += nftw(args[0], do_SetMetadataRecursive, 30, FTW_CHDIR | FTW_DEPTH | FTW_PHYS);
        memset(&recursive_parsed_args, 0, sizeof(recursive_parsed_args));
    } else {
        bad += ApplyParsedPerms(args[0], &sb, parsed);
    }

done:
    for (i = 0; i < argc; ++i) {
        free(args[i]);
    }
    free(args);

    if (result != NULL) {
        return result;
    }

    if (bad > 0) {
        return ErrorAbort(state, "%s: some changes failed", name);
    }

    return StringValue(strdup(""));
}

Value* GetPropFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* key;
    key = Evaluate(state, argv[0]);
    if (key == NULL) return NULL;

    char value[PROPERTY_VALUE_MAX];
    property_get(key, value, "");
    free(key);

    return StringValue(strdup(value));
}


// file_getprop(file, key)
//
//   interprets 'file' as a getprop-style file (key=value pairs, one
//   per line, # comment lines and blank lines okay), and returns the value
//   for 'key' (or "" if it isn't defined).
Value* FileGetPropFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    char* buffer = NULL;
    char* filename;
    char* key;
    if (ReadArgs(state, argv, 2, &filename, &key) < 0) {
        return NULL;
    }

    struct stat st;
    if (stat(filename, &st) < 0) {
        ErrorAbort(state, "%s: failed to stat \"%s\": %s",
                   name, filename, strerror(errno));
        goto done;
    }

#define MAX_FILE_GETPROP_SIZE    65536

    if (st.st_size > MAX_FILE_GETPROP_SIZE) {
        ErrorAbort(state, "%s too large for %s (max %d)",
                   filename, name, MAX_FILE_GETPROP_SIZE);
        goto done;
    }

    buffer = malloc(st.st_size+1);
    if (buffer == NULL) {
        ErrorAbort(state, "%s: failed to alloc %lld bytes", name, st.st_size+1);
        goto done;
    }

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        ErrorAbort(state, "%s: failed to open %s: %s",
                   name, filename, strerror(errno));
        goto done;
    }

    if (fread(buffer, 1, st.st_size, f) != st.st_size) {
        ErrorAbort(state, "%s: failed to read %lld bytes from %s",
                   name, st.st_size+1, filename);
        fclose(f);
        goto done;
    }
    buffer[st.st_size] = '\0';

    fclose(f);

    char* line = strtok(buffer, "\n");
    do {
        // skip whitespace at start of line
        while (*line && isspace(*line)) ++line;

        // comment or blank line: skip to next line
        if (*line == '\0' || *line == '#') continue;

        char* equal = strchr(line, '=');
        if (equal == NULL) {
            ErrorAbort(state, "%s: malformed line \"%s\": %s not a prop file?",
                       name, line, filename);
            goto done;
        }

        // trim whitespace between key and '='
        char* key_end = equal-1;
        while (key_end > line && isspace(*key_end)) --key_end;
        key_end[1] = '\0';

        // not the key we're looking for
        if (strcmp(key, line) != 0) continue;

        // skip whitespace after the '=' to the start of the value
        char* val_start = equal+1;
        while(*val_start && isspace(*val_start)) ++val_start;

        // trim trailing whitespace
        char* val_end = val_start + strlen(val_start)-1;
        while (val_end > val_start && isspace(*val_end)) --val_end;
        val_end[1] = '\0';

        result = strdup(val_start);
        break;

    } while ((line = strtok(NULL, "\n")));

    if (result == NULL) result = strdup("");

  done:
    free(filename);
    free(key);
    free(buffer);
    return StringValue(result);
}


static bool write_raw_image_cb(const unsigned char* data,
                               int data_len, void* ctx) {
    int r = mtd_write_data((MtdWriteContext*)ctx, (const char *)data, data_len);
    if (r == data_len) return true;
    fprintf(stderr, "%s\n", strerror(errno));
    return false;
}

// write_raw_image(filename_or_blob, partition)
Value* WriteRawImageFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;

    Value* partition_value;
    Value* contents;
    if (ReadValueArgs(state, argv, 2, &contents, &partition_value) < 0) {
        return NULL;
    }

    char* partition = NULL;
    if (partition_value->type != VAL_STRING) {
        ErrorAbort(state, "partition argument to %s must be string", name);
        goto done;
    }
    partition = partition_value->data;
    if (strlen(partition) == 0) {
        ErrorAbort(state, "partition argument to %s can't be empty", name);
        goto done;
    }
    if (contents->type == VAL_STRING && strlen((char*) contents->data) == 0) {
        ErrorAbort(state, "file argument to %s can't be empty", name);
        goto done;
    }

#if 1 //wschen 2011-11-24 for eMMC update logo/dsp_bl/uboot/preloader
    if (!strcmp(partition, "logo") || !strcmp(partition, "preloader") || !strcmp(partition, "uboot") || !strcmp(partition, "dsp_bl") || !strcmp(partition, "bootimg") || !strcmp(partition, "boot")|| !strcmp(partition, "tee1")) {
        FILE *fp = fopen("/proc/dumchar_info", "r");
        if (fp) {
            char buf[512], p_name[32], p_size[32], p_addr[32], p_actname[64];
            int p_type = 0;
            if (fgets(buf, sizeof(buf), fp) == NULL) {
                fclose(fp);
                goto done;
            }
            while (fgets(buf, sizeof(buf), fp)) {
                if (sscanf(buf, "%s %s %s %d %s", p_name, p_size, p_addr, &p_type, p_actname) == 5) {
                    if (!strcmp(p_name, "bmtpool")) {
                        break;
                    }

                    if (strlen(p_name) && (!strcmp(p_name, "boot") || !strcmp(p_name, "bootimg"))) {

                        //phone is eMMC but recovery.fstab is NAND
                        if ((p_type == 2) && !strcmp(partition, "boot")) {
                            free(partition);
                            partition = strdup("bootimg");
                            partition_value->data = partition;
                        } else if ((p_type == 1) && !strcmp(partition, "boot")) {
                            //phone is NAND and recovery.fstab is NAND
                            break;
                        } else if ((p_type == 1) && !strcmp(partition, "bootimg")) {
                            //phone is NAND but recovery.fstab is eMMC
                            free(partition);
                            partition = strdup("boot");
                            partition_value->data = partition;
                            break;
                        }
                    }

                    if (strlen(p_name) && !strcmp(partition, p_name)) {
                        if (p_type == 2) {
                            char part_name[128];
                            snprintf(part_name, 128, "/dev/%s", p_name);
                            int fd = open(part_name, O_WRONLY | O_SYNC);
                            if (fd != -1) {
                                char rbuf[512];
                                int len;
                                int in_fd;
                                int ok = 1;

                                if (contents->type == VAL_STRING) {

                                    in_fd = open(contents->data, O_RDONLY);
                                    if (in_fd != -1) {
                                        while ((len = read(in_fd, rbuf, 512)) > 0) {
                                            if (write(fd, rbuf, len) == -1) {
                                                ok = 0;
                                                break;
                                            }
                                        }
                                        close(in_fd);
                                    } else {
                                        ok = 0;
                                    }
                                    close(fd);
                                    if (ok) {
                                        sync();
                                        result = partition;
                                    }
                                } else {

                                    if (write(fd, contents->data, contents->size) != contents->size) {
                                        close(fd);
                                        break;
                                    } else {
                                        close(fd);
                                        sync();
                                        result = partition;
                                    }
                                }
                            }

                            fclose(fp);
                            goto done;
                        }
                    }
                }
            }
            fclose(fp);
        } else {
            goto done;
        }
    }
#endif

    mtd_scan_partitions();
    const MtdPartition* mtd = mtd_find_partition_by_name(partition);
    if (mtd == NULL) {
        fprintf(stderr, "%s: no mtd partition named \"%s\"\n", name, partition);
        result = strdup("");
        goto done;
    }

    MtdWriteContext* ctx = mtd_write_partition(mtd);
    if (ctx == NULL) {
        fprintf(stderr, "%s: can't write mtd partition \"%s\"\n",
                name, partition);
        result = strdup("");
        goto done;
    }

    bool success;

    if (contents->type == VAL_STRING) {
        // we're given a filename as the contents
        char* filename = contents->data;
        FILE* f = fopen(filename, "rb");
        if (f == NULL) {
            fprintf(stderr, "%s: can't open %s: %s\n",
                    name, filename, strerror(errno));
            result = strdup("");
            goto done;
        }

        success = true;
        char* buffer = malloc(BUFSIZ);
        int read;
        while (success && (read = fread(buffer, 1, BUFSIZ, f)) > 0) {
            int wrote = mtd_write_data(ctx, buffer, read);
            success = success && (wrote == read);
        }
        free(buffer);
        fclose(f);
    } else {
        // we're given a blob as the contents
        ssize_t wrote = mtd_write_data(ctx, contents->data, contents->size);
        success = (wrote == contents->size);
    }
    if (!success) {
        fprintf(stderr, "mtd_write_data to %s failed: %s\n",
                partition, strerror(errno));
    }

    if (mtd_erase_blocks(ctx, -1) == (off64_t)-1) {
        fprintf(stderr, "%s: error erasing blocks of %s\n", name, partition);
    }
    if (mtd_write_close(ctx) != 0) {
        fprintf(stderr, "%s: error closing write of %s\n", name, partition);
    }

    printf("%s %s partition\n",
           success ? "wrote" : "failed to write", partition);

    result = success ? partition : strdup("");

done:
    if (result != partition) FreeValue(partition_value);
    FreeValue(contents);
    return StringValue(result);
}

// apply_patch_space(bytes)
Value* ApplyPatchSpaceFn(const char* name, State* state,
                         int argc, Expr* argv[]) {
    char* bytes_str;
    if (ReadArgs(state, argv, 1, &bytes_str) < 0) {
        return NULL;
    }

    char* endptr;
    size_t bytes = strtol(bytes_str, &endptr, 10);
    if (bytes == 0 && endptr == bytes_str) {
        ErrorAbort(state, "%s(): can't parse \"%s\" as byte count\n\n",
                   name, bytes_str);
        free(bytes_str);
        return NULL;
    }

    return StringValue(strdup(CacheSizeCheck(bytes) ? "" : "t"));
}


// apply_patch(srcfile, tgtfile, tgtsha1, tgtsize, sha1_1, patch_1, ...)
Value* ApplyPatchFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc < 6 || (argc % 2) == 1) {
        return ErrorAbort(state, "%s(): expected at least 6 args and an "
                                 "even number, got %d",
                          name, argc);
    }

    char* source_filename;
    char* target_filename;
    char* target_sha1;
    char* target_size_str;
    if (ReadArgs(state, argv, 4, &source_filename, &target_filename,
                 &target_sha1, &target_size_str) < 0) {
        return NULL;
    }

    char* endptr;
    size_t target_size = strtol(target_size_str, &endptr, 10);
    if (target_size == 0 && endptr == target_size_str) {
        ErrorAbort(state, "%s(): can't parse \"%s\" as byte count",
                   name, target_size_str);
        free(source_filename);
        free(target_filename);
        free(target_sha1);
        free(target_size_str);
        return NULL;
    }

    int patchcount = (argc-4) / 2;
    Value** patches = ReadValueVarArgs(state, argc-4, argv+4);

    int i;
    for (i = 0; i < patchcount; ++i) {
        if (patches[i*2]->type != VAL_STRING) {
            ErrorAbort(state, "%s(): sha-1 #%d is not string", name, i);
            break;
        }
        if (patches[i*2+1]->type != VAL_BLOB) {
            ErrorAbort(state, "%s(): patch #%d is not blob", name, i);
            break;
        }
    }
    if (i != patchcount) {
        for (i = 0; i < patchcount*2; ++i) {
            FreeValue(patches[i]);
        }
        free(patches);
        return NULL;
    }

    char** patch_sha_str = malloc(patchcount * sizeof(char*));
    for (i = 0; i < patchcount; ++i) {
        patch_sha_str[i] = patches[i*2]->data;
        patches[i*2]->data = NULL;
        FreeValue(patches[i*2]);
        patches[i] = patches[i*2+1];
    }

    int result = applypatch(source_filename, target_filename,
                            target_sha1, target_size,
                            patchcount, patch_sha_str, patches, NULL);

    for (i = 0; i < patchcount; ++i) {
        FreeValue(patches[i]);
    }
    free(patch_sha_str);
    free(patches);

    return StringValue(strdup(result == 0 ? "t" : ""));
}

// apply_patch_check(file, [sha1_1, ...])
Value* ApplyPatchCheckFn(const char* name, State* state,
                         int argc, Expr* argv[]) {
    if (argc < 1) {
        return ErrorAbort(state, "%s(): expected at least 1 arg, got %d",
                          name, argc);
    }

    char* filename;
    if (ReadArgs(state, argv, 1, &filename) < 0) {
        return NULL;
    }

    int patchcount = argc-1;
    char** sha1s = ReadVarArgs(state, argc-1, argv+1);

    int result = applypatch_check(filename, patchcount, sha1s);

    int i;
    for (i = 0; i < patchcount; ++i) {
        free(sha1s[i]);
    }
    free(sha1s);

    return StringValue(strdup(result == 0 ? "t" : ""));
}

Value* UIPrintFn(const char* name, State* state, int argc, Expr* argv[]) {
    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) {
        return NULL;
    }

    int size = 0;
    int i;
    for (i = 0; i < argc; ++i) {
        size += strlen(args[i]);
    }
    char* buffer = malloc(size+1);
    size = 0;
    for (i = 0; i < argc; ++i) {
        strcpy(buffer+size, args[i]);
        size += strlen(args[i]);
        free(args[i]);
    }
    free(args);
    buffer[size] = '\0';

    char* line = strtok(buffer, "\n");
    while (line) {
        fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe,
                "ui_print %s\n", line);
        line = strtok(NULL, "\n");
    }
    fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe, "ui_print\n");

    return StringValue(buffer);
}

Value* WipeCacheFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 0) {
        return ErrorAbort(state, "%s() expects no args, got %d", name, argc);
    }
    fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe, "wipe_cache\n");
    return StringValue(strdup("t"));
}

#if 1 //wschen 2012-07-25
Value* SpecialFactoryResetFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 0) {
        return ErrorAbort(state, "%s() expects no args, got %d", name, argc);
    }
    fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe, "special_factory_reset\n");
    return StringValue(strdup("t"));
}
#endif

Value* RunProgramFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc < 1) {
        return ErrorAbort(state, "%s() expects at least 1 arg", name);
    }
    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) {
        return NULL;
    }

    char** args2 = malloc(sizeof(char*) * (argc+1));
    memcpy(args2, args, sizeof(char*) * argc);
    args2[argc] = NULL;

    fprintf(stderr, "about to run program [%s] with %d args\n", args2[0], argc);

    pid_t child = fork();
    if (child == 0) {
        execv(args2[0], args2);
        fprintf(stderr, "run_program: execv failed: %s\n", strerror(errno));
        _exit(1);
    }
    int status;
    waitpid(child, &status, 0);
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0) {
            fprintf(stderr, "run_program: child exited with status %d\n",
                    WEXITSTATUS(status));
        }
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "run_program: child terminated by signal %d\n",
                WTERMSIG(status));
    }

    int i;
    for (i = 0; i < argc; ++i) {
        free(args[i]);
    }
    free(args);
    free(args2);

    char buffer[20];
    sprintf(buffer, "%d", status);

    return StringValue(strdup(buffer));
}

// Take a sha-1 digest and return it as a newly-allocated hex string.
static char* PrintSha1(uint8_t* digest) {
    char* buffer = malloc(SHA_DIGEST_SIZE*2 + 1);
    int i;
    const char* alphabet = "0123456789abcdef";
    for (i = 0; i < SHA_DIGEST_SIZE; ++i) {
        buffer[i*2] = alphabet[(digest[i] >> 4) & 0xf];
        buffer[i*2+1] = alphabet[digest[i] & 0xf];
    }
    buffer[i*2] = '\0';
    return buffer;
}

// sha1_check(data)
//    to return the sha1 of the data (given in the format returned by
//    read_file).
//
// sha1_check(data, sha1_hex, [sha1_hex, ...])
//    returns the sha1 of the file if it matches any of the hex
//    strings passed, or "" if it does not equal any of them.
//
Value* Sha1CheckFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc < 1) {
        return ErrorAbort(state, "%s() expects at least 1 arg", name);
    }

    Value** args = ReadValueVarArgs(state, argc, argv);
    if (args == NULL) {
        return NULL;
    }

    if (args[0]->size < 0) {
        fprintf(stderr, "%s(): no file contents received", name);
        return StringValue(strdup(""));
    }
    uint8_t digest[SHA_DIGEST_SIZE];
    SHA_hash(args[0]->data, args[0]->size, digest);
    FreeValue(args[0]);

    if (argc == 1) {
        return StringValue(PrintSha1(digest));
    }

    int i;
    uint8_t* arg_digest = malloc(SHA_DIGEST_SIZE);
    for (i = 1; i < argc; ++i) {
        if (args[i]->type != VAL_STRING) {
            fprintf(stderr, "%s(): arg %d is not a string; skipping",
                    name, i);
        } else if (ParseSha1(args[i]->data, arg_digest) != 0) {
            // Warn about bad args and skip them.
            fprintf(stderr, "%s(): error parsing \"%s\" as sha-1; skipping",
                   name, args[i]->data);
        } else if (memcmp(digest, arg_digest, SHA_DIGEST_SIZE) == 0) {
            break;
        }
        FreeValue(args[i]);
    }
    if (i >= argc) {
        // Didn't match any of the hex strings; return false.
        return StringValue(strdup(""));
    }
    // Found a match; free all the remaining arguments and return the
    // matched one.
    int j;
    for (j = i+1; j < argc; ++j) {
        FreeValue(args[j]);
    }
    return args[i];
}

// Read a local file and return its contents (the Value* returned
// is actually a FileContents*).
Value* ReadFileFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* filename;
    if (ReadArgs(state, argv, 1, &filename) < 0) return NULL;

    Value* v = malloc(sizeof(Value));
    v->type = VAL_BLOB;

    FileContents fc;
    if (LoadFileContents(filename, &fc, RETOUCH_DONT_MASK) != 0) {
        ErrorAbort(state, "%s() loading \"%s\" failed: %s",
                   name, filename, strerror(errno));
        free(filename);
        free(v);
        free(fc.data);
        return NULL;
    }

    v->size = fc.size;
    v->data = (char*)fc.data;

    free(filename);
    return v;
}

//Start [LenovoOTA][miaotao1] 2012-11-7 
Value* WriteFileFN(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 arg, got %d", name, argc);
    }
    char* file_name = NULL;
    FileContents fileContent;
    fileContent.data = NULL;
    if (ReadArgs(state, argv, 2, &file_name, &fileContent.data) < 0) {
        return NULL;
    }

    if (strlen(file_name) == 0) {
        ErrorAbort(state, "write_file file_name can't be empty");
        goto done;
    }
    fileContent.size = strlen(fileContent.data)*sizeof(char);
    fileContent.st.st_mode = S_IFREG | S_IROTH | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    fileContent.st.st_uid  = 0;
    fileContent.st.st_gid  = 0;

    if (SaveFileContents(file_name, &fileContent) == 0) {
        //OK
        result = file_name;
    } else {
        //Error
        fprintf(stderr, "write to file %s failed \n", file_name);
        result = strdup("");
    }

done:
    if (result != file_name) free(file_name);
    if (fileContent.data != NULL) free(fileContent.data),fileContent.data=NULL;
    return StringValue(result);
}

//End [LenovoOTA][miaotao1]



void RegisterInstallFunctions() {
    RegisterFunction("mount", MountFn);
    RegisterFunction("is_mounted", IsMountedFn);
    RegisterFunction("unmount", UnmountFn);
    RegisterFunction("format", FormatFn);
    RegisterFunction("show_progress", ShowProgressFn);
    RegisterFunction("set_progress", SetProgressFn);
    RegisterFunction("delete", DeleteFn);
    RegisterFunction("delete_recursive", DeleteFn);
    RegisterFunction("package_extract_dir", PackageExtractDirFn);
    RegisterFunction("package_extract_file", PackageExtractFileFn);
    RegisterFunction("symlink", SymlinkFn);

    // Maybe, at some future point, we can delete these functions? They have been
    // replaced by perm_set and perm_set_recursive.
    RegisterFunction("set_perm", SetPermFn);
    RegisterFunction("set_perm_recursive", SetPermFn);

    // Usage:
    //   set_metadata("filename", "key1", "value1", "key2", "value2", ...)
    // Example:
    //   set_metadata("/system/bin/netcfg", "uid", 0, "gid", 3003, "mode", 02750, "selabel", "u:object_r:system_file:s0", "capabilities", 0x0);
    RegisterFunction("set_metadata", SetMetadataFn);

    // Usage:
    //   set_metadata_recursive("dirname", "key1", "value1", "key2", "value2", ...)
    // Example:
    //   set_metadata_recursive("/system", "uid", 0, "gid", 0, "fmode", 0644, "dmode", 0755, "selabel", "u:object_r:system_file:s0", "capabilities", 0x0);
    RegisterFunction("set_metadata_recursive", SetMetadataFn);

    RegisterFunction("getprop", GetPropFn);
    RegisterFunction("file_getprop", FileGetPropFn);
    RegisterFunction("write_raw_image", WriteRawImageFn);

    RegisterFunction("apply_patch", ApplyPatchFn);
    RegisterFunction("apply_patch_check", ApplyPatchCheckFn);
    RegisterFunction("apply_patch_space", ApplyPatchSpaceFn);

    RegisterFunction("read_file", ReadFileFn);
    RegisterFunction("sha1_check", Sha1CheckFn);
    RegisterFunction("rename", RenameFn);

    RegisterFunction("wipe_cache", WipeCacheFn);

#if 1 //wschen 2012-07-25
    RegisterFunction("special_factory_reset", SpecialFactoryResetFn);
#endif

    RegisterFunction("ui_print", UIPrintFn);

    RegisterFunction("run_program", RunProgramFn);
//Start [LenovoOTA][miaotao1] 2012-11-7 
    RegisterFunction("write_file", WriteFileFN);
//End [LenovoOTA][miaotao1]
#ifdef MTK_SYS_FW_UPGRADE
    RegisterFunction("retouch_binaries_ext", RetouchBinariesFnExt);
    RegisterFunction("undo_retouch_binaries_ext", UndoRetouchBinariesFnExt);
    RegisterFunction("apply_data_app", ApplyDataAppsFn);
#endif
}
