/**
 *
 * *
 *
 * uring time peroformance to write 1GB to file
 */


#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/time.h>
#include <string.h>

int main()
{
        struct timeval start, stop;
double secs = 0;

    int times=7;
    struct io_uring ring;
    io_uring_queue_init(10, &ring, 0);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    int fd = open("test.txt", O_RDONLY | O_CREAT,0644);
    int buffer_size=3;
    char* data=(char*)malloc(buffer_size*2);
    //memset(data,'a',buffer_size);
    struct iovec* iovs = calloc(2, sizeof(struct iovec));

    for(int i=0;i<2;++i)
    {
        iovs[i].iov_base=data+i*buffer_size;
        iovs[i].iov_len=buffer_size;
    }


    gettimeofday(&start, NULL);

 
    struct io_uring_cqe *cqe;

    for (int i=0;i<times;++i) {
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_readv(sqe, fd, iovs, 2, buffer_size*i*2);
        io_uring_submit(&ring);
        io_uring_wait_cqe(&ring,&cqe);
        printf(data);
    }
    printf("\n");

 


    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    close(fd);
    gettimeofday(&stop, NULL);





secs = (double)(stop.tv_usec - start.tv_usec);
printf("time taken %f\n",(stop.tv_sec-start.tv_sec)*1.0+(stop.tv_usec-start.tv_usec)/1000000.0);

}



