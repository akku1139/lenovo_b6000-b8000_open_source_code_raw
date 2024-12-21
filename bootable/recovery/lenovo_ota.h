#ifndef LENOVO_OTA_H_
#define LENOVO_OTA_H_

#ifdef __cplusplus
extern "C" {
#endif

void lenovo_ota_remove_package_and_write_result(const char *update_package, const int status);
const char * lenovo_ota_fix_patch_location(const char * update_package);

#ifdef __cplusplus
}
#endif
#endif 
