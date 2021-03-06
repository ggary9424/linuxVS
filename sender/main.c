#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "v4l2_api.h"

#define EXEC_CMD_AND_CHECK(cmd, return_value, message) do { \
                                if ((cmd) == return_value) { \
                                    printf(#message" fail.\n"); \
                                    exit(EXIT_FAILURE); \
                                } \
                                printf(#message" exec.\n"); \
                            } while (0);

#define SET_HEADER(header, t, w, h) do { \
                                header.time = t; \
                                header.width = w; \
                                header.height = h; \
                            } while(0);

typedef struct RGB {
    int r;
    int g;
    int b;
} RGB;

typedef struct HEADER {
    struct timeval time;
    int width;
    int height;
} Header;

static inline int clip(int value, int min, int max)
{
    return (value > max ? max : (value < min ? min : value));
}

static inline RGB YUV_to_RGB(int y, int u, int v)
{
    RGB rgb;
    int r,g,b;
    u = u -128;
    v = v-128;
    r = (298 * y + 409 * v + 128) >> 8;
    g = (298 * y - 100 * u - 208 * v + 128) >> 8;
    b = (298 * y + 516 * u + 128) >> 8;
    rgb.r = clip(r, 0, 255);
    rgb.b = clip(b, 0, 255);
    rgb.g = clip(g, 0, 255);
    return rgb;
}

static void YUYV_to_RGB_file(const void * p, const int width, const int height, const char *filename)
{
    unsigned char* in = (unsigned char*)p;
    int y0,y1,u,v;
    RGB rgb;

    FILE *outfile = fopen(filename, "wb");
    fprintf(outfile, "P6\n%d %d\n255\n", width, height);
    for (int y=0; y<height; y++) {
        for (int x=0; x<width/2; x++) {
            int tmp = x*4;
            /* YUYV */
            y0 = in[tmp];
            y1 = in[tmp+2];
            u = in[tmp+1];
            v = in[tmp+3];

            rgb = YUV_to_RGB(y0, u, v);
            fwrite(&rgb.r, 1, 1, outfile);
            fwrite(&rgb.g, 1, 1, outfile);
            fwrite(&rgb.b, 1, 1, outfile);

            rgb = YUV_to_RGB(y1, u, v);
            fwrite(&rgb.r, 1, 1, outfile);
            fwrite(&rgb.g, 1, 1, outfile);
            fwrite(&rgb.b, 1, 1, outfile);
        }
        in += width*2;
    }
    fclose(outfile);
}

int main(void)
{
    int fd = 0, req_buffer_num = 4, width = 720, height = 600;
    char *video = "/dev/video0";
    my_buffer *bufs = NULL;
    char *pic = NULL;
    int socketfd = -1;
    struct sockaddr_in toaddr;
    int len = 0, pic_size;
    /* TODO: use it to make reliable header */
    struct timeval timenow;
    Header header;

    EXEC_CMD_AND_CHECK(fd = v4l2_open_dev(video), -1, v4l2_open_dev);
    EXEC_CMD_AND_CHECK(v4l2_init_dev(fd, &req_buffer_num, &bufs, &width, &height), -1, v4l2_init_dev);
    EXEC_CMD_AND_CHECK(v4l2_start_capstream(fd, req_buffer_num), -1, v4l2_start_capstream);
    EXEC_CMD_AND_CHECK(pic = v4l2_getpic(fd, bufs), NULL, v4l2_getpic);
    //YUYV_to_RGB_file(pic, width, height, "pic.ppm");

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd == -1) {
        perror("socket create error!\n");
        exit(EXIT_FAILURE);
    }
    toaddr.sin_family = AF_INET;
    toaddr.sin_port = htons(8080);
    toaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if ((connect(socketfd, (struct sockaddr*)&toaddr, sizeof(toaddr))) == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    while (1) {
        EXEC_CMD_AND_CHECK(pic = (char *)v4l2_getpic(fd, bufs), NULL, v4l2_getpic);
        pic_size = width*height*2;
        gettimeofday(&timenow, NULL);
        SET_HEADER(header, timenow, width, height);
        // UDP max length is 65507
        if ((len = send(socketfd, &header, sizeof(header), 0)) == -1) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
        if ((len = send(socketfd, pic, pic_size, 0)) == -1) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }
    }

    close(socketfd);

    EXEC_CMD_AND_CHECK(v4l2_stop_capstream(fd), -1, v4l2_stop_capstream);
    EXEC_CMD_AND_CHECK(v4l2_munmap_bufs(req_buffer_num, &bufs), -1, v4l2_munmap_bufs);
    EXEC_CMD_AND_CHECK(v4l2_close_dev(fd), -1, v4l2_close_dev);
    return 0;
}
