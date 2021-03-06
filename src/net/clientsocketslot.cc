#include "clientsocketslot.h"
#include "systemmsg.h"
#include <sys/socket.h>
#include "../base/log.h"

using namespace net;
using namespace base;

CClientSocketSlot::CClientSocketSlot()
{
    m_ClientRSA.GenerateKey();
}

CClientSocketSlot::~CClientSocketSlot()
{
}

void CClientSocketSlot::OnConnect(int nFd, const sockaddr_in &rSockAddr)
{
    m_nFd = nFd;
    m_addr = rSockAddr.sin_addr;
    m_nPort = ntohs(rSockAddr.sin_port);

    SetNonBlocking(m_nFd);

    _SetState(SocketState_Free, SocketState_Accepting);

    // Generate public key
    //unsigned char * pPublicKey = NULL;
    //int nKeySize = m_ClientRSA.GetPublicKey(&pPublicKey);
    //char szN[3 * cMAX_PART_OF_PUBLIC_KEY_LEN] = {0};
    //char szE[16] = {0};
    char * szN = NULL;
    char * szE = NULL;
    m_ClientRSA.GetPublicKey(&szN, &szE);

    unsigned int nNSize = strlen(szN);
    // ASSERT(nKeySize > cMAX_PART_OF_PUBLIC_KEY_LEN);
    MSG_SYSTEM_ClientPublicKey * pCPKMsg = new MSG_SYSTEM_ClientPublicKey();
    pCPKMsg->nSrcNSize1 = cMAX_PART_OF_PUBLIC_KEY_LEN;
    m_pServerRSA->PublicEncrypt((unsigned char *)szN, cMAX_PART_OF_PUBLIC_KEY_LEN, pCPKMsg->n1);
    pCPKMsg->nSrcNSize2 = cMAX_PART_OF_PUBLIC_KEY_LEN;
    m_pServerRSA->PublicEncrypt((unsigned char *)(szN + cMAX_PART_OF_PUBLIC_KEY_LEN), cMAX_PART_OF_PUBLIC_KEY_LEN, pCPKMsg->n2);
    pCPKMsg->nSrcNSize3 = nNSize - 2 * cMAX_PART_OF_PUBLIC_KEY_LEN;
    m_pServerRSA->PublicEncrypt((unsigned char *)(szN + 2 * cMAX_PART_OF_PUBLIC_KEY_LEN), nNSize - 2 * cMAX_PART_OF_PUBLIC_KEY_LEN, pCPKMsg->n3);
    pCPKMsg->nSrcESize = strlen(szE);
    m_pServerRSA->PublicEncrypt((unsigned char *)szE, pCPKMsg->nSrcESize, pCPKMsg->e);
    WriteLog("szN=%s.strlen(szN)=%d. n1=%d, n2=%d, n3=%d. szE=%s, strlen(szE)=%d.\n", szN, nNSize, pCPKMsg->nSrcNSize1, pCPKMsg->nSrcNSize2, pCPKMsg->nSrcNSize3, szE, strlen(szE));

    if (!_SendMsgDirectly(pCPKMsg))
    {
        WriteLog(LEVEL_WARNING, "CClientSocketSlot::OnConnect. Send client public key failed.\n");
    }
    //OPENSSL_free(pPublicKey);
    OPENSSL_free(szN);
    OPENSSL_free(szE);

    /*
    // Add SocketConnectSuccessMsg to Recv queue
    MSG_SYSTEM_SocketConnectSuccess * pSCSMsg = new MSG_SYSTEM_SocketConnectSuccess();
    if (!_AddRecvMsg(pSCSMsg))
    {
    delete pSCSMsg;
    pSCSMsg = NULL;
    WriteLog(LEVEL_ERROR, "CClientSocketSlot::OnConnect. Add connect success msg failed.\n");
    }
     */
}

void CClientSocketSlot::Init(LoopQueue< CRecvDataElement * > * pRecvQueue, int nEpollFd, CRSA * pServerRSA, bool bEncrypt, bool bCompress, unsigned char * pSendDataBuffer, unsigned char * pUncompressBuffer)
{
    _SetRecvQueue(pRecvQueue);
    _SetEpollFd(nEpollFd);
    _SetServerRSA(pServerRSA);
    _SetEncrypt(bEncrypt);
    _SetCompress(bCompress);
    _SetSendDataBuffer(pSendDataBuffer);
    _SetUncompressBuffer(pUncompressBuffer);
}

void CClientSocketSlot::Reset()
{
    _Reset();
}

bool CClientSocketSlot::NeedSendData()
{
    if (m_pMsgCache != NULL)
    {
        return true;
    }
    if (!m_SendQueue.IsEmpty())
    {
        return true;
    }
    if (m_pSendDataHead != m_pSendDataTail)
    {
        return true;
    }
    return false;
}
bool CClientSocketSlot::_DisposeRecvMsg(MSG_BASE & rMsg)
{
    bool bRes = true;
    switch (rMsg.nMsg)
    {
        case MSGID_SYSTEM_S2C_SecretKey:
            {
                MSG_SYSTEM_S2C_SecretKey & rSKMsg = (MSG_SYSTEM_S2C_SecretKey &)rMsg;

                unsigned char szPublicKey1[64];
                unsigned char szPublicKey2[64];
                int nKeySize1 = m_pServerRSA->PublicDecrypt(rSKMsg.key, 64, szPublicKey1);
                if (nKeySize1 != 32)
                {
                    WriteLog(LEVEL_WARNING, "CClientSocketSlot::_DisposeRecvMsg. nKeySize1(%d) != 32.\n", nKeySize1);
                    return false;
                }
                int nKeySize2 = m_pServerRSA->PublicDecrypt(rSKMsg.key + 64, 64, szPublicKey2);
                if (nKeySize2 != 32)
                {
                    WriteLog(LEVEL_WARNING, "CClientSocketSlot::_DisposeRecvMsg. nKeySize2(%d) != 32.\n", nKeySize2);
                    return false;
                }
                unsigned char szPublicKey[64];
                memcpy(szPublicKey, szPublicKey1, 32);
                memcpy(szPublicKey+32, szPublicKey2, 32);
                unsigned char key[64];
                int nKeySize = m_ClientRSA.PrivateDecrypt(szPublicKey, 64, key);
                if (nKeySize != cSECRET_KEY_LEN)
                {
                    WriteLog(LEVEL_WARNING, "CClientSocketSlot::_DisposeRecvMsg. nKeySize(%d) != cSECRET_KEY_LEN(%d).\n", nKeySize, cSECRET_KEY_LEN);
                    return false;
                }

                m_EncryptRC4.SetKey(key, nKeySize);
                m_DecryptRC4.SetKey(key, nKeySize);


                _SetState(SocketState_Accepting, SocketState_Normal);

                MSG_SYSTEM_ConnectSuccess * pConnectSuccessMsg = new MSG_SYSTEM_ConnectSuccess();
                if (!_AddRecvMsg(pConnectSuccessMsg))
                {
                    delete pConnectSuccessMsg;
                    pConnectSuccessMsg = NULL;
                    WriteLog(LEVEL_ERROR, "CClientSocketSlot::_DisposeRecvMsg. Add connect success msg failed.\n");
                    bRes = false;
                }
            }
            break;
        case MSGID_SYSTEM_S2C_UpdateSecretKey:
            {
                // TODO : Decrypt
                // m_DecryptKey = "";
            }
            break;
        default:
            bRes = CSocketSlot::_DisposeRecvMsg(rMsg);
            break;
    }
    return bRes;
}
