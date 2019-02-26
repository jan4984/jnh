#include <iostream>

#define DEBUG_LOG
#include "jnh.h"

int main() {
    int code;
    char body[128] = {0};
    int bodyLen = sizeof(body) - 1;
    int ret = jnh_get("easybox.iflyos.cn", 80, "/time_now", 1000000, 1000000, &code, body, &bodyLen);
    printf("ret:%d\n, status:%d, body:%s\n", ret, code, body);
    return 0;
}