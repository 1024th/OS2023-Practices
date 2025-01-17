#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

using namespace std;

int main() {
  // 得到套接字描述符
  int sockfd;
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    cout << "socket error" << endl;
    exit(-1);
  }

  struct ifconf ifc;
  caddr_t buf;
  int len = 100;

  // 初始化ifconf结构
  ifc.ifc_len = 1024;
  if ((buf = (caddr_t)malloc(1024)) == NULL) {
    cout << "malloc error" << endl;
    exit(-1);
  }
  ifc.ifc_buf = buf;

  // 获取所有接口信息
  if (ioctl(sockfd, SIOCGIFCONF, (char *)&ifc) < 0) {
    cout << "ioctl error" << endl;
    exit(-1);
  }

  // 遍历每一个ifreq结构
  struct ifreq *ifr;
  struct ifreq ifrcopy;
  ifr = (struct ifreq *)buf;
  for (int i = (ifc.ifc_len / sizeof(struct ifreq)); i > 0; i--) {
    // 接口名
    cout << "interface name: " << ifr->ifr_name << endl;
    // ipv4地址
    cout << "inet addr: " << inet_ntoa(((struct sockaddr_in *)&(ifr->ifr_addr))->sin_addr) << endl;

    // 获取广播地址
    ifrcopy = *ifr;
    if (ioctl(sockfd, SIOCGIFBRDADDR, &ifrcopy) < 0) {
      cout << "ioctl error" << endl;
      exit(-1);
    }

    cout << "broad addr: " << inet_ntoa(((struct sockaddr_in *)&(ifrcopy.ifr_addr))->sin_addr) << endl;
    // 获取mtu
    ifrcopy = *ifr;
    if (ioctl(sockfd, SIOCGIFMTU, &ifrcopy) < 0) {
      cout << "ioctl error" << endl;
      exit(-1);
    }

    cout << "mtu: " << ifrcopy.ifr_mtu << endl;
    cout << endl;
    ifr++;
  }

  return 0;
}