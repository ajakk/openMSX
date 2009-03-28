// $Id$

#ifdef _WIN32

#include "SocketStreamWrapper.hh"

namespace openmsx {

SocketStreamWrapper::SocketStreamWrapper(SOCKET userSock)
	: sock(userSock)
{
}

unsigned int SocketStreamWrapper::Read(void* buffer, unsigned int cb)
{
	int recvd = recv(sock, static_cast<char*>(buffer), cb, 0);
	if (recvd == 0 || recvd == SOCKET_ERROR) {
		return STREAM_ERROR;
	}
	return recvd;
}

unsigned int SocketStreamWrapper::Write(void* buffer, unsigned int cb)
{
	int sent = send(sock, static_cast<char*>(buffer), cb, 0);
	if (sent == 0 || sent == SOCKET_ERROR) {
		return STREAM_ERROR;
	}
	return sent;
}

} // namespace openmsx

#endif
