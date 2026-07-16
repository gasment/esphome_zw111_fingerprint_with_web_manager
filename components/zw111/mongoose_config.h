// mongoose_config.h
#pragma once
#define MG_ARCH MG_ARCH_ESP32
#define MG_ENABLE_TCPIP 0
#define MG_TLS MG_TLS_NONE
#define MG_IO_SIZE 512
#define MG_ENABLE_DIRLIST 0    // 禁用目录服务, 消除 opendir/closedir 警告
#define MG_ENABLE_PACKED_FS 0
#define MG_ENABLE_LOG 0        // 禁用Mongoose内部日志
