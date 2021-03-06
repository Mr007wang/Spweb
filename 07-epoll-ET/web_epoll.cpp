#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "csapp.h"
#include "epoll_ulti.h"
#include "http_handle.h"
#include "state.h"

/*-
* 非阻塞版本的web server,主要利用epoll机制来实现多路IO复用.
* 与之前的epoll版本使用了LT触发有所不同的是,这里的epoll机制使用了ET触发.对于采用了LT工作模式的文件描述符,
* 当epoll_wait检测到其上有事件发生并且将事件通知应用程序后,应用程序可以不立即处理该事件,这样,当应用程序下次
* 调用epoll_wiat时,epoll_wait还会向应用程序通告此事件,直到该事件被处理.而对于采用ET工作模式的文件描述符,当
* epoll_wait检测到其上有事件发生并且将此事件通知应用程序后,应用程序必须立即处理该事件,因为后续的epoll_wait调用
* 将不会向应用程序通知这一事件,可见,ET模式在很大程度上降低了同一个epoll事件被重复触发的次数,因此效率比LT模式要
* 高.
*/

/* 网页的根目录 */
const char * root_dir = "/home/lishuhuakai/WebSiteSrc/html_book_20150808/reference";
/* / 所指代的网页 */
const char * home_page = "index.html";
#define MAXEVENTNUM 100

void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if (restart) {
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[])
{
	int listenfd = Open_listenfd(8080); /* 8080号端口监听 */
	epoll_event events[MAXEVENTNUM];
	sockaddr clnaddr;
	socklen_t clnlen = sizeof(clnaddr);

	addsig(SIGPIPE, SIG_IGN);
	
	int epollfd = Epoll_create(80); /* 10基本上没有什么用处 */
	addfd(epollfd, listenfd, false); /* epollfd要监听listenfd上的可读事件 */
	
	HttpHandle handle[256];
	int acnt = 0;
	for ( ; ;) {
		int eventnum = Epoll_wait(epollfd, events, MAXEVENTNUM, -1);
		for (int i = 0; i < eventnum; ++i) {
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd) { /* 有连接到来 */
				printf("%d\n", ++acnt);
				for ( ; ; ) {
					int connfd = accept(listenfd, &clnaddr, &clnlen);
					if (connfd == -1) {
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) { /* 将连接已经建立完了 */
							break;
						}
						unix_error("accept error");
					}
					handle[connfd].init(connfd); /* 初始化 */
					addfd(epollfd, connfd, false); /* 加入监听 */
				}
			}
			else if (events[i].events & EPOLLIN) { /* 有数据可读 */
				int res = handle[sockfd].processRead(); /* 处理读事件 */
				if (res == STATUS_WRITE)  /* 我们需要监听写事件 */
					modfd(epollfd, sockfd, EPOLLOUT);
				else 
					removefd(epollfd, sockfd);
			}
			else if (events[i].events & EPOLLOUT) { /* 如果可写了 */
				printf("Could write!\n");
				int res = handle[sockfd].processWrite(); /* 处理写事件 */
				if (res == STATUS_READ) /* 对方发送了keepalive */
					modfd(epollfd, sockfd, EPOLLIN);
				else
					removefd(epollfd, sockfd);
			}
		}
	}
	return 0;
}

