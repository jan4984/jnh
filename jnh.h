//
// Created by jan on 19-2-26.
//

#ifndef JNH_JNH_H
#define JNH_JNH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JNHE_OK 0
#define JNHE_FD_FAIL -100
#define JNHE_DNS -200
#define JNHE_CLOCK -300
#define JNHE_TIMEOUT -400
#define JNHE_CONNECT -500
#define JNHE_WRITE -600
#define JNHE_READ -700
#define JNHE_HTTP -800

#ifdef DEBUG_LOG
#define LOG printf("errno:%d\n", errno)
#else
#define LOG
#endif

static inline int jnh_get(
        const char* host,
        const int portno,
        const char* URLpath,
        int timeoutAllMs,
        int timeoutAfterDNSMs,
        int* rspCode,
        char* body,
        int* bodyLen) {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time)){
        return JNHE_CLOCK;
    }

    long startDNS = time.tv_sec * 1000 + time.tv_nsec / 1000000;

    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        return JNHE_FD_FAIL;
    }

    #define ERROR(n) do{close(sockfd); return n;}while(0)
    server = gethostbyname(host);

    if(clock_gettime(CLOCK_MONOTONIC, &time)){
        ERROR(JNHE_CLOCK);
    }
    long startConnect = time.tv_sec * 1000 + time.tv_nsec / 1000000;

    if(startConnect - startDNS >= timeoutAllMs) {
        ERROR(JNHE_TIMEOUT);
    }

    if (server == NULL) {
        ERROR(JNHE_DNS);
    }

    // Set non-blocking
    int arg = fcntl(sockfd, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, arg);

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    int connectRet = connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if(connectRet < 0 && errno != EINPROGRESS){
        ERROR(JNHE_CONNECT);
    }

    if(connectRet < 0){
        if(clock_gettime(CLOCK_MONOTONIC, &time)){
            ERROR(JNHE_CLOCK);
        }
        long startSelectConnect = time.tv_sec * 1000 + time.tv_nsec / 1000000;
        long timeout1 = timeoutAllMs - (startSelectConnect - startDNS);
        if(timeout1 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        long timeout2 = timeoutAfterDNSMs - (startSelectConnect - startConnect);
        if( timeout2 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        struct timeval tv;
        fd_set fs;
        tv.tv_sec = 0;
        tv.tv_usec = (timeout1 < timeout2 ? timeout1 : timeout2) * 1000;
        FD_ZERO(&fs);
        FD_SET(sockfd, &fs);
        if (select(sockfd+1, NULL, &fs, NULL, &tv) > 0) {
            int valopt;
            socklen_t lon = sizeof(int);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void *) (&valopt), &lon);
            if (valopt) {
                ERROR(JNHE_CONNECT);
            }
        }else{
            ERROR(JNHE_TIMEOUT);
        }
    }

    int wr;
#define WC(n) do{wr = write(sockfd, n, sizeof(n) - 1); if(wr < 0) ERROR(JNHE_WRITE);}while(0)
#define W(n) do{wr = write(sockfd, n, strlen(n)); if(wr < 0) ERROR(JNHE_WRITE);}while(0)
    WC("GET ");
    W(URLpath);
    WC(" HTTP/1.1\r\n");//WE NOT SUPPORT CHUNK-ED transfer
    WC("Host: ");
    W(host);
    char portnoBuf[10];
    sprintf(portnoBuf, ":%d", portno);
    W(portnoBuf);
    WC("\r\nConnection: close");
    WC("\r\n\r\n");

    int idx = 0;
    char beforeBodyLine[128] = {0};
    *rspCode = -1;
    //get response code and find body
    do{
        if(clock_gettime(CLOCK_MONOTONIC, &time)){
            ERROR(JNHE_CLOCK);
        }
        long now = time.tv_sec * 1000 + time.tv_nsec / 1000000;
        long timeout1 = timeoutAllMs - (now - startDNS);
        if(timeout1 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        long timeout2 = timeoutAfterDNSMs - (now - startConnect);
        if( timeout2 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        struct timeval tv;
        fd_set fs;
        tv.tv_sec = 0;
        tv.tv_usec = (timeout1 < timeout2 ? timeout1 : timeout2) * 1000;
        FD_ZERO(&fs);
        FD_SET(sockfd, &fs);
        if (select(sockfd+1, &fs, NULL, &fs, &tv) > 0) {
            int rd = recv(sockfd, &beforeBodyLine[idx], sizeof(beforeBodyLine) - idx, 0);
            if(rd <= 0){
                LOG;
                ERROR(JNHE_READ);
            }
            if(*rspCode == -1) {
                //I will say the "HTTP/1.0 200 OK\r\n" can not in two recv()
                char *lb = strstr(beforeBodyLine, "\r\n");
                if (!lb) ERROR(JNHE_HTTP);
                char *code = &beforeBodyLine[sizeof("HTTP/1.1")];
                *rspCode = atoi(code);
            }

            char* bodyPtr=strstr(beforeBodyLine, "\n\r\n");
            if(bodyPtr){
                int len = rd - (bodyPtr + 3 - beforeBodyLine);
                idx = 0;
                if(*bodyLen <= len){
                    memcpy(&body[idx], bodyPtr + 3, *bodyLen);
                    ERROR(JNHE_OK);
                }
                memcpy(&body[idx], bodyPtr + 3, len);
                idx += len;
                if(len == 0){
                    *bodyLen = 0;
                    ERROR(JNHE_OK);
                }
                break;
            }

            char* tail = &beforeBodyLine[idx + rd];
            char buf[4];
            idx = 0;
            while(*tail == '\r' || *tail == '\n'){
                buf[idx++] = *tail;
                tail--;
            }
            for(int i = 0; i < idx; i++){
                beforeBodyLine[i] = buf[idx - i - 1];
            }
        }else{
            ERROR(JNHE_TIMEOUT);
        }
    }while(true);

    //fill body
    do{
        if(clock_gettime(CLOCK_MONOTONIC, &time)){
            ERROR(JNHE_CLOCK);
        }
        long now = time.tv_sec * 1000 + time.tv_nsec / 1000000;
        long timeout1 = timeoutAllMs - (now - startDNS);
        if(timeout1 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        long timeout2 = timeoutAfterDNSMs - (now - startConnect);
        if( timeout2 <= 0){
            ERROR(JNHE_TIMEOUT);
        }
        struct timeval tv;
        fd_set fs;
        tv.tv_sec = 0;
        tv.tv_usec = (timeout1 < timeout2 ? timeout1 : timeout2) * 1000;
        FD_ZERO(&fs);
        FD_SET(sockfd, &fs);
        if (select(sockfd+1, &fs, NULL, &fs, &tv) > 0) {
            int rd = recv(sockfd, &body[idx], *bodyLen - idx, MSG_DONTWAIT);
            if(rd < 0){
                LOG;
                ERROR(JNHE_READ);
            }
            if(rd == 0){
                *bodyLen = idx;
                ERROR(JNHE_OK);
            }
            idx+=rd;
        }else{
            ERROR(JNHE_TIMEOUT);
        }
    }while(true);
}

#ifdef __cplusplus
}
#endif

#endif //JNH_JNH_H
