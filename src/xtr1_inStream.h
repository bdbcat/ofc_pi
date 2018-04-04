/***************************************************************************
 * 
 * Project:  OpenCPN
 * Purpose:  XTR1_INSTREAM Object
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2018 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 **************************************************************************/

#ifndef XTR1_INSTREAM_H
#define XTR1_INSTREAM_H

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
#include "wx/wx.h"
#endif //precompiled headers


#include <string.h>
#include <stdint.h>

#ifndef __WXMSW__
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#endif

class wxFileInputStream;
//--------------------------------------------------------------------------
//      Utility prototypes
//--------------------------------------------------------------------------

#define MALLOC_BLOCK_SIZE 256
#define PALLETE_LINE_SIZE 32
#define REF_LINE_SIZE 64
#define PLY_LINE_SIZE 64


typedef struct{
    int Size_X;
    int Size_Y;
    int Chart_DU;
    int Chart_Scale;
    float Chart_Skew;
    char Name[128];
    char ID[64];
    char SoundDatum[32];
    char DepthUnits[32];
    char Projection[32];
    char GeoDatum[32];
    char SE[8];
    char ED[16];
    float ProjParm;
    float DX;
    float DY;
    int nPalleteLines;
    int nRefLines;
    int nPlyLines;
    int nColorSize;
    
    float DTMlat;
    float DTMlon;
    float CPH;
    
    double WPX[12];
    double WPY[12];
    double PWX[12];
    double PWY[12];
    
} compressedHeader;

struct fifo_msg {
    char cmd;
    char fifo_name[256];
    char file_name[256];
    char crypto_key[256];
};

#define PUBLIC "/tmp/OCPN_PIPE_XTR1"

#define CMD_READ_ESENC          0
#define CMD_TEST_AVAIL          1
#define CMD_EXIT                2
#define CMD_READ_ESENC_HDR      3
#define CMD_OPEN                4
#define CMD_DECRYPT_BZB         5
#define CMD_GET_HK              6



void resetBSBHeaderBuffer();
char getNextChar();
size_t readBSBHeaderBufferedLine( const char *fileName, char *lineBuffer, size_t nLineLen );
bool getCompressedHeaderBase( const char *fileName, compressedHeader *hdr);
bool processHeader();
bool processOffsetTable();



//--------------------------------------------------------------------------
//      xtr1_inStream definition
//--------------------------------------------------------------------------
class xtr1_inStream 
{
public:
    xtr1_inStream();
    xtr1_inStream( const wxString &file_name, const wxString &crypto_key = wxEmptyString );
    ~xtr1_inStream();
    
    void Init();
    void ReInit();
    void Close();
    bool Open( );
    bool Load( );
    
    bool SendServerCommand( unsigned char cmd );
    bool SendServerCommand( fifo_msg *pmsg );
    bool decryptBZB(wxString &inFile, wxString outFile);
    
    xtr1_inStream &Read(void *buffer, size_t size);
    bool IsOk();
    bool Ok();
    
    bool isAvailable(wxString user_key);
    void Shutdown();
    wxString getHK();
    
    compressedHeader *GetCompressedHeader();
    char *GetPalleteBlock();
    char *GetRefBlock();
    char *GetPlyBlock();
    
    off_t GetBitmapOffset( unsigned int y );
    
    
private:
    int privatefifo; // file descriptor to read-end of PRIVATE
    int publicfifo; // file descriptor to write-end of PUBLIC 
    
    char privatefifo_name[256];
    bool m_OK;
    int m_lastBytesRead, m_lastBytesReq;
   
    wxString m_fileName;
    wxString m_cryptoKey;
    
#ifdef __WXMSW__    
    HANDLE hPipe;
    LPCWSTR lpszPipename;
#endif    
    
#ifndef __WXMSW__    
    char publicsocket_name[256];
    struct sockaddr_un sockAddr;
    socklen_t sockLen;
    int publicSocket;
#endif
    
    wxFileInputStream *m_uncrypt_stream;                // for LOCAL_FILE test only
    
    char err[100];
    compressedHeader *phdr;
    char *pPalleteBlock;
    char *pRefBlock;
    char *pPlyBlock;
    int *pline_table;           // pointer to Line offset table
    
};

#endif