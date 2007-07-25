/** @file

Copyright (c) 2006 - 2007, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.


Module Name:

  Udp4Io.c

Abstract:

  Help functions to access UDP service, it is used by both the DHCP and MTFTP.


**/

#include <PiDxe.h>

#include <Protocol/Udp4.h>

#include <Library/UdpIoLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC
VOID
EFIAPI
UdpIoOnDgramSent (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  );

STATIC
VOID
EFIAPI
UdpIoOnDgramRcvd (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  );


/**
  Wrap a transmit request into a UDP_TX_TOKEN.

  @param  UdpIo                 The UdpIo port to send packet to
  @param  Packet                The user's packet
  @param  EndPoint              The local and remote access point
  @param  Gateway               The overrided next hop
  @param  CallBack              The function to call when transmission completed.
  @param  Context               The opaque parameter to the call back

  @return The wrapped transmission request or NULL if failed to allocate resources.

**/
STATIC
UDP_TX_TOKEN *
UdpIoWrapTx (
  IN UDP_IO_PORT            *UdpIo,
  IN NET_BUF                *Packet,
  IN UDP_POINTS             *EndPoint, OPTIONAL
  IN IP4_ADDR               Gateway,
  IN UDP_IO_CALLBACK        CallBack,
  IN VOID                   *Context
  )
{
  UDP_TX_TOKEN              *Token;
  EFI_UDP4_COMPLETION_TOKEN *UdpToken;
  EFI_UDP4_TRANSMIT_DATA    *UdpTxData;
  EFI_STATUS                Status;
  UINT32                    Count;

  Token = NetAllocatePool (sizeof (UDP_TX_TOKEN) +
                           sizeof (EFI_UDP4_FRAGMENT_DATA) * (Packet->BlockOpNum - 1));

  if (Token == NULL) {
    return NULL;
  }

  Token->Signature  = UDP_IO_TX_SIGNATURE;
  NetListInit (&Token->Link);

  Token->UdpIo      = UdpIo;
  Token->CallBack   = CallBack;
  Token->Packet     = Packet;
  Token->Context    = Context;

  UdpToken          = &(Token->UdpToken);
  UdpToken->Status  = EFI_NOT_READY;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UdpIoOnDgramSent,
                  Token,
                  &UdpToken->Event
                  );

  if (EFI_ERROR (Status)) {
    NetFreePool (Token);
    return NULL;
  }

  UdpTxData                 = &Token->UdpTxData;
  UdpToken->Packet.TxData   = UdpTxData;

  UdpTxData->UdpSessionData = NULL;
  UdpTxData->GatewayAddress = NULL;

  if (EndPoint != NULL) {
    EFI_IP4 (Token->UdpSession.SourceAddress)      = HTONL (EndPoint->LocalAddr);
    EFI_IP4 (Token->UdpSession.DestinationAddress) = HTONL (EndPoint->RemoteAddr);
    Token->UdpSession.SourcePort                   = EndPoint->LocalPort;
    Token->UdpSession.DestinationPort              = EndPoint->RemotePort;
    UdpTxData->UdpSessionData                      = &Token->UdpSession;
  }

  if (Gateway != 0) {
    EFI_IP4 (Token->Gateway)  = HTONL (Gateway);
    UdpTxData->GatewayAddress = &Token->Gateway;
  }

  UdpTxData->DataLength = Packet->TotalSize;
  Count                 = Packet->BlockOpNum;
  NetbufBuildExt (Packet, (NET_FRAGMENT *) UdpTxData->FragmentTable, &Count);
  UdpTxData->FragmentCount = Count;

  return Token;
}


/**
  Free a UDP_TX_TOKEN. The event is closed and memory released.

  @param  Token                 The UDP_TX_TOKEN to release.

  @return None

**/
VOID
UdpIoFreeTxToken (
  IN UDP_TX_TOKEN           *Token
  )
{
  gBS->CloseEvent (Token->UdpToken.Event);
  NetFreePool (Token);
}


/**
  Create a UDP_RX_TOKEN to wrap the request.

  @param  UdpIo                 The UdpIo to receive packets from
  @param  CallBack              The function to call when receive finished.
  @param  Context               The opaque parameter to the CallBack
  @param  HeadLen               The head length to reserver for the packet.

  @return The Wrapped request or NULL if failed to allocate resources.

**/
UDP_RX_TOKEN *
UdpIoCreateRxToken (
  IN UDP_IO_PORT            *UdpIo,
  IN UDP_IO_CALLBACK        CallBack,
  IN VOID                   *Context,
  IN UINT32                 HeadLen
  )
{
  UDP_RX_TOKEN              *Token;
  EFI_STATUS                Status;

  Token = NetAllocatePool (sizeof (UDP_RX_TOKEN));

  if (Token == NULL) {
    return NULL;
  }

  Token->Signature              = UDP_IO_RX_SIGNATURE;
  Token->UdpIo                  = UdpIo;
  Token->CallBack               = CallBack;
  Token->Context                = Context;
  Token->HeadLen                = HeadLen;

  Token->UdpToken.Status        = EFI_NOT_READY;
  Token->UdpToken.Packet.RxData = NULL;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UdpIoOnDgramRcvd,
                  Token,
                  &Token->UdpToken.Event
                  );

  if (EFI_ERROR (Status)) {
    NetFreePool (Token);
    return NULL;
  }

  return Token;
}


/**
  Free a receive request wrap.

  @param  Token                 The receive request to release.

  @return None

**/
VOID
UdpIoFreeRxToken (
  IN UDP_RX_TOKEN           *Token
  )
{
  gBS->CloseEvent (Token->UdpToken.Event);
  NetFreePool (Token);
}


/**
  Create a UDP IO port to access the UDP service. It will
  create and configure a UDP child.

  @param  Controller            The controller that has the UDP service binding
                                protocol installed.
  @param  Image                 The image handle for the driver.
  @param  Configure             The function to configure the created UDP child
  @param  Context               The opaque parameter for the Configure funtion.

  @return A point to just created UDP IO port or NULL if failed.

**/
UDP_IO_PORT *
UdpIoCreatePort (
  IN  EFI_HANDLE            Controller,
  IN  EFI_HANDLE            Image,
  IN  UDP_IO_CONFIG         Configure,
  IN  VOID                  *Context
  )
{
  UDP_IO_PORT               *UdpIo;
  EFI_STATUS                Status;

  ASSERT (Configure != NULL);

  UdpIo = NetAllocatePool (sizeof (UDP_IO_PORT));

  if (UdpIo == NULL) {
    return NULL;
  }

  UdpIo->Signature    = UDP_IO_SIGNATURE;
  NetListInit (&UdpIo->Link);
  UdpIo->RefCnt       = 1;

  UdpIo->Controller   = Controller;
  UdpIo->Image        = Image;

  NetListInit (&UdpIo->SentDatagram);
  UdpIo->RecvRequest  = NULL;
  UdpIo->UdpHandle    = NULL;

  //
  // Create a UDP child then open and configure it
  //
  Status = NetLibCreateServiceChild (
             Controller,
             Image,
             &gEfiUdp4ServiceBindingProtocolGuid,
             &UdpIo->UdpHandle
             );

  if (EFI_ERROR (Status)) {
    goto FREE_MEM;
  }

  Status = gBS->OpenProtocol (
                  UdpIo->UdpHandle,
                  &gEfiUdp4ProtocolGuid,
                  (VOID **) &UdpIo->Udp,
                  Image,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    goto FREE_CHILD;
  }

  if (EFI_ERROR (Configure (UdpIo, Context))) {
    goto CLOSE_PROTOCOL;
  }

  Status = UdpIo->Udp->GetModeData (UdpIo->Udp, NULL, NULL, NULL, &UdpIo->SnpMode);

  if (EFI_ERROR (Status)) {
    goto CLOSE_PROTOCOL;
  }

  return UdpIo;

CLOSE_PROTOCOL:
  gBS->CloseProtocol (UdpIo->UdpHandle, &gEfiUdp4ProtocolGuid, Image, Controller);

FREE_CHILD:
  NetLibDestroyServiceChild (
    Controller,
    Image,
    &gEfiUdp4ServiceBindingProtocolGuid,
    UdpIo->UdpHandle
    );

FREE_MEM:
  NetFreePool (UdpIo);
  return NULL;
}


/**
  Cancel all the sent datagram that pass the selection of ToCancel.
  If ToCancel is NULL, all the datagrams are cancelled.

  @param  UdpIo                 The UDP IO port to cancel packet
  @param  IoStatus              The IoStatus to return to the packet owners.
  @param  ToCancel              The select funtion to test whether to cancel this
                                packet or not.
  @param  Context               The opaque parameter to the ToCancel.

  @return None

**/
STATIC
VOID
UdpIoCancelDgrams (
  IN UDP_IO_PORT            *UdpIo,
  IN EFI_STATUS             IoStatus,
  IN UDP_IO_TO_CANCEL       ToCancel,        OPTIONAL
  IN VOID                   *Context
  )
{
  NET_LIST_ENTRY            *Entry;
  NET_LIST_ENTRY            *Next;
  UDP_TX_TOKEN              *Token;

  NET_LIST_FOR_EACH_SAFE (Entry, Next, &UdpIo->SentDatagram) {
    Token = NET_LIST_USER_STRUCT (Entry, UDP_TX_TOKEN, Link);

    if ((ToCancel == NULL) || (ToCancel (Token, Context))) {
      NetListRemoveEntry (Entry);
      UdpIo->Udp->Cancel (UdpIo->Udp, &Token->UdpToken);
      Token->CallBack (Token->Packet, NULL, IoStatus, Token->Context);
      UdpIoFreeTxToken (Token);
    }
  }
}


/**
  Free the UDP IO port and all its related resources including
  all the transmitted packet.

  @param  UdpIo                 The UDP IO port to free.

  @retval EFI_SUCCESS           The UDP IO port is freed.

**/
EFI_STATUS
UdpIoFreePort (
  IN  UDP_IO_PORT           *UdpIo
  )
{
  UDP_RX_TOKEN  *RxToken;

  //
  // Cancel all the sent datagram and receive requests. The
  // callbacks of transmit requests are executed to allow the
  // caller to release the resource. The callback of receive
  // request are NOT executed. This is because it is most
  // likely that the current user of the UDP IO port is closing
  // itself.
  //
  UdpIoCancelDgrams (UdpIo, EFI_ABORTED, NULL, NULL);

  if ((RxToken = UdpIo->RecvRequest) != NULL) {
    UdpIo->RecvRequest = NULL;
    UdpIo->Udp->Cancel (UdpIo->Udp, &RxToken->UdpToken);
    UdpIoFreeRxToken (RxToken);
  }

  //
  // Close then destory the UDP child
  //
  gBS->CloseProtocol (
         UdpIo->UdpHandle,
         &gEfiUdp4ProtocolGuid,
         UdpIo->Image,
         UdpIo->Controller
         );

  NetLibDestroyServiceChild (
    UdpIo->Controller,
    UdpIo->Image,
    &gEfiUdp4ServiceBindingProtocolGuid,
    UdpIo->UdpHandle
    );

  NetListRemoveEntry (&UdpIo->Link);
  NetFreePool (UdpIo);
  return EFI_SUCCESS;
}


/**
  Clean up the UDP IO port. It will release all the transmitted
  datagrams and receive request. It will also configure NULL the
  UDP child.

  @param  UdpIo                 UDP IO port to clean up.

  @return None

**/
VOID
UdpIoCleanPort (
  IN  UDP_IO_PORT           *UdpIo
  )
{
  UDP_RX_TOKEN              *RxToken;

  //
  // Cancel all the sent datagram and receive requests.
  //
  UdpIoCancelDgrams (UdpIo, EFI_ABORTED, NULL, NULL);

  if ((RxToken = UdpIo->RecvRequest) != NULL) {
    UdpIo->RecvRequest = NULL;
    UdpIo->Udp->Cancel (UdpIo->Udp, &RxToken->UdpToken);
    UdpIoFreeRxToken (RxToken);
  }

  UdpIo->Udp->Configure (UdpIo->Udp, NULL);
}


/**
  The callback function when the packet is sent by UDP.
  It will remove the packet from the local list then call
  the packet owner's callback function.

  @param  Event                 The event signalled.
  @param  Context               The UDP TX Token.

  @return None

**/
STATIC
VOID
EFIAPI
UdpIoOnDgramSent (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  )
{
  UDP_TX_TOKEN              *Token;

  Token   = (UDP_TX_TOKEN *) Context;
  ASSERT (Token->Signature == UDP_IO_TX_SIGNATURE);

  NetListRemoveEntry (&Token->Link);
  Token->CallBack (Token->Packet, NULL, Token->UdpToken.Status, Token->Context);

  UdpIoFreeTxToken (Token);
}


/**
  Send a packet through the UDP IO port.

  @param  UdpIo                 The UDP IO Port to send the packet through
  @param  Packet                The packet to send
  @param  EndPoint              The local and remote access point
  @param  Gateway               The gateway to use
  @param  CallBack              The call back function to call when packet is
                                transmitted or failed.
  @param  Context               The opque parameter to the CallBack

  @retval EFI_OUT_OF_RESOURCES  Failed to allocate resource for the packet
  @retval EFI_SUCCESS           The packet is successfully delivered to UDP  for
                                transmission.

**/
EFI_STATUS
UdpIoSendDatagram (
  IN  UDP_IO_PORT           *UdpIo,
  IN  NET_BUF               *Packet,
  IN  UDP_POINTS            *EndPoint, OPTIONAL
  IN  IP4_ADDR              Gateway,
  IN  UDP_IO_CALLBACK       CallBack,
  IN  VOID                  *Context
  )
{
  UDP_TX_TOKEN              *Token;
  EFI_STATUS                Status;

  Token = UdpIoWrapTx (UdpIo, Packet, EndPoint, Gateway, CallBack, Context);

  if (Token == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = UdpIo->Udp->Transmit (UdpIo->Udp, &Token->UdpToken);

  if (EFI_ERROR (Status)) {
    UdpIoFreeTxToken (Token);
    return Status;
  }

  NetListInsertHead (&UdpIo->SentDatagram, &Token->Link);
  return EFI_SUCCESS;
}


/**
  The selection function to cancel a single sent datagram.

  @param  Token                 The UDP TX token to test againist.
  @param  Context               The context

  @return TRUE if the packet is to be cancelled, otherwise FALSE.

**/
STATIC
BOOLEAN
UdpIoCancelSingleDgram (
  IN UDP_TX_TOKEN           *Token,
  IN VOID                   *Context
  )
{
  NET_BUF                   *Packet;

  Packet = (NET_BUF *) Context;

  if (Token->Packet == Packet) {
    return TRUE;
  }

  return FALSE;
}


/**
  Cancel a single sent datagram.

  @param  UdpIo                 The UDP IO port to cancel the packet from
  @param  Packet                The packet to cancel

  @return None

**/
VOID
UdpIoCancelSentDatagram (
  IN  UDP_IO_PORT           *UdpIo,
  IN  NET_BUF               *Packet
  )
{
  UdpIoCancelDgrams (UdpIo, EFI_ABORTED, UdpIoCancelSingleDgram, Packet);
}


/**
  Recycle the received UDP data.

  @param  Context               The UDP_RX_TOKEN

  @return None

**/
STATIC
VOID
UdpIoRecycleDgram (
  IN VOID                   *Context
  )
{
  UDP_RX_TOKEN              *Token;

  Token = (UDP_RX_TOKEN *) Context;
  gBS->SignalEvent (Token->UdpToken.Packet.RxData->RecycleSignal);
  UdpIoFreeRxToken (Token);
}


/**
  The event handle for UDP receive request. It will build
  a NET_BUF from the recieved UDP data, then deliver it
  to the receiver.

  @param  Event                 The UDP receive request event
  @param  Context               The UDP RX token.

  @return None

**/
STATIC
VOID
EFIAPI
UdpIoOnDgramRcvd (
  IN EFI_EVENT              Event,
  IN VOID                   *Context
  )
{
  EFI_UDP4_COMPLETION_TOKEN *UdpToken;
  EFI_UDP4_RECEIVE_DATA     *UdpRxData;
  EFI_UDP4_SESSION_DATA     *UdpSession;
  UDP_RX_TOKEN              *Token;
  UDP_POINTS                Points;
  NET_BUF                   *Netbuf;

  Token   = (UDP_RX_TOKEN *) Context;

  ASSERT ((Token->Signature == UDP_IO_RX_SIGNATURE) &&
          (Token == Token->UdpIo->RecvRequest));

  //
  // Clear the receive request first in case that the caller
  // wants to restart the receive in the callback.
  //
  Token->UdpIo->RecvRequest = NULL;

  UdpToken  = &Token->UdpToken;
  UdpRxData = UdpToken->Packet.RxData;

  if (EFI_ERROR (UdpToken->Status) || (UdpRxData == NULL)) {
    Token->CallBack (NULL, NULL, UdpToken->Status, Token->Context);
    UdpIoFreeRxToken (Token);

    goto ON_EXIT;
  }

  //
  // Build a NET_BUF from the UDP receive data, then deliver it up.
  //
  Netbuf = NetbufFromExt (
             (NET_FRAGMENT *) UdpRxData->FragmentTable,
             UdpRxData->FragmentCount,
             0,
             (UINT32) Token->HeadLen,
             UdpIoRecycleDgram,
             Token
             );

  if (Netbuf == NULL) {
    gBS->SignalEvent (UdpRxData->RecycleSignal);
    Token->CallBack (NULL, NULL, EFI_OUT_OF_RESOURCES, Token->Context);

    UdpIoFreeRxToken (Token);
    goto ON_EXIT;
  }

  UdpSession        = &UdpRxData->UdpSession;
  Points.LocalAddr  = EFI_NTOHL (UdpSession->DestinationAddress);
  Points.LocalPort  = UdpSession->DestinationPort;
  Points.RemoteAddr = EFI_NTOHL (UdpSession->SourceAddress);
  Points.RemotePort = UdpSession->SourcePort;

  Token->CallBack (Netbuf, &Points, EFI_SUCCESS, Token->Context);

ON_EXIT:
  return;
}


/**
  Issue a receive request to the UDP IO port.

  @param  UdpIo                 The UDP IO port to recieve the packet from.
  @param  CallBack              The call back function to execute when receive
                                finished.
  @param  Context               The opque context to the call back
  @param  HeadLen               The lenght of the application's header

  @retval EFI_ALREADY_STARTED   There is already a pending receive request. Only
                                one receive request is supported.
  @retval EFI_OUT_OF_RESOURCES  Failed to allocate some resource.
  @retval EFI_SUCCESS           The receive request is issued successfully.

**/
EFI_STATUS
UdpIoRecvDatagram (
  IN  UDP_IO_PORT           *UdpIo,
  IN  UDP_IO_CALLBACK       CallBack,
  IN  VOID                  *Context,
  IN  UINT32                HeadLen
  )
{
  UDP_RX_TOKEN              *Token;
  EFI_STATUS                Status;

  if (UdpIo->RecvRequest != NULL) {
    return EFI_ALREADY_STARTED;
  }

  Token = UdpIoCreateRxToken (UdpIo, CallBack, Context, HeadLen);

  if (Token == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = UdpIo->Udp->Receive (UdpIo->Udp, &Token->UdpToken);

  if (EFI_ERROR (Status)) {
    UdpIoFreeRxToken (Token);
    return Status;
  }

  UdpIo->RecvRequest = Token;
  return EFI_SUCCESS;
}
