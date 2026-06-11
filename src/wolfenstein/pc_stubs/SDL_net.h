// Local SDL_net stub for the desktop (app_pc) test build, so a
// single-player data-load test does not require the sdl2_net package.
// Mirrors the cross-build stubs in ../../sdk/include/SDL_net.h; shadows
// it via an earlier -I path. Networking is never exercised by the test.
#ifndef OF_ECWOLF_SDL_NET_STUB_H
#define OF_ECWOLF_SDL_NET_STUB_H

#include <SDL2/SDL.h>
#include <stdlib.h>

typedef int UDPsocket;
typedef struct { unsigned int host; unsigned short port; } IPaddress;
typedef struct
{
    int channel;
    Uint8 *data;
    int len;
    int maxlen;
    int status;
    IPaddress address;
} UDPpacket;

static inline int SDLNet_Init(void) { return 0; }
static inline void SDLNet_Quit(void) {}
static inline UDPsocket SDLNet_UDP_Open(unsigned short port) { (void)port; return 0; }
static inline void SDLNet_UDP_Close(UDPsocket sock) { (void)sock; }
static inline UDPpacket *SDLNet_AllocPacket(int size)
{
    UDPpacket *packet = (UDPpacket *)calloc(1, sizeof(*packet));
    if (packet == NULL)
        return NULL;
    packet->data = (Uint8 *)calloc(1, size);
    if (packet->data == NULL)
    {
        free(packet);
        return NULL;
    }
    packet->channel = -1;
    packet->maxlen = size;
    return packet;
}
static inline void SDLNet_FreePacket(UDPpacket *packet)
{
    if (packet != NULL)
    {
        free(packet->data);
        free(packet);
    }
}
static inline int SDLNet_UDP_Send(UDPsocket sock, int channel, UDPpacket *packet) { (void)sock; (void)channel; (void)packet; return 0; }
static inline int SDLNet_UDP_Recv(UDPsocket sock, UDPpacket *packet) { (void)sock; (void)packet; return 0; }
static inline int SDLNet_ResolveHost(IPaddress *address, const char *host, unsigned short port) { (void)address; (void)host; (void)port; return -1; }
static inline const char *SDLNet_GetError(void) { return ""; }

#endif
