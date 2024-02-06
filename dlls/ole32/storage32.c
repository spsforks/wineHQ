/*
 * Compound Storage (32 bit version)
 * Storage implementation
 *
 * This file contains the compound file implementation
 * of the storage interface.
 *
 * Copyright 1999 Francis Beaudet
 * Copyright 1999 Sylvain St-Germain
 * Copyright 1999 Thuy Nguyen
 * Copyright 2005 Mike McCormack
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * NOTES
 *  The compound file implementation of IStorage used for create
 *  and manage substorages and streams within a storage object
 *  residing in a compound file object.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winuser.h"
#include "wine/debug.h"

#include "ole2.h"      /* For Write/ReadClassStm */

#include "winreg.h"
#include "wine/wingdi16.h"
#include "compobj_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(storage);

static const BYTE STORAGE_magic[8]    ={0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1};

/***********************************************************************
 *    OleLoadFromStream (OLE32.@)
 *
 * This function loads an object from stream
 */
HRESULT  WINAPI OleLoadFromStream(IStream *pStm,REFIID iidInterface,void** ppvObj)
{
    CLSID	clsid;
    HRESULT	res;
    LPPERSISTSTREAM	xstm;

    TRACE("(%p,%s,%p)\n",pStm,debugstr_guid(iidInterface),ppvObj);

    res=ReadClassStm(pStm,&clsid);
    if (FAILED(res))
	return res;
    res=CoCreateInstance(&clsid,NULL,CLSCTX_INPROC_SERVER,iidInterface,ppvObj);
    if (FAILED(res))
	return res;
    res=IUnknown_QueryInterface((IUnknown*)*ppvObj,&IID_IPersistStream,(LPVOID*)&xstm);
    if (FAILED(res)) {
	IUnknown_Release((IUnknown*)*ppvObj);
	return res;
    }
    res=IPersistStream_Load(xstm,pStm);
    IPersistStream_Release(xstm);
    /* FIXME: all refcounts ok at this point? I think they should be:
     * 		pStm	: unchanged
     *		ppvObj	: 1
     *		xstm	: 0 (released)
     */
    return res;
}

/***********************************************************************
 *    OleSaveToStream (OLE32.@)
 *
 * This function saves an object with the IPersistStream interface on it
 * to the specified stream.
 */
HRESULT  WINAPI OleSaveToStream(IPersistStream *pPStm,IStream *pStm)
{

    CLSID clsid;
    HRESULT res;

    TRACE("(%p,%p)\n",pPStm,pStm);

    res=IPersistStream_GetClassID(pPStm,&clsid);

    if (SUCCEEDED(res)){

        res=WriteClassStm(pStm,&clsid);

        if (SUCCEEDED(res))

            res=IPersistStream_Save(pPStm,pStm,TRUE);
    }

    TRACE("Finished Save\n");
    return res;
}

/*************************************************************************
 * STORAGE_CreateOleStream [Internal]
 *
 * Creates the "\001OLE" stream in the IStorage if necessary.
 *
 * PARAMS
 *     storage     [I] Dest storage to create the stream in
 *     flags       [I] flags to be set for newly created stream
 *
 * RETURNS
 *     HRESULT return value
 *
 * NOTES
 *
 *     This stream is still unknown, MS Word seems to have extra data
 *     but since the data is stored in the OLESTREAM there should be
 *     no need to recreate the stream.  If the stream is manually
 *     deleted it will create it with this default data.
 *
 */
HRESULT STORAGE_CreateOleStream(IStorage *storage, DWORD flags)
{
    static const DWORD version_magic = 0x02000001;
    IStream *stream;
    HRESULT hr;

    hr = IStorage_CreateStream(storage, L"\1Ole", STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stream);
    if (hr == S_OK)
    {
        struct empty_1ole_stream {
            DWORD version_magic;
            DWORD flags;
            DWORD update_options;
            DWORD reserved;
            DWORD mon_stream_size;
        };
        struct empty_1ole_stream stream_data;

        stream_data.version_magic = version_magic;
        stream_data.flags = flags;
        stream_data.update_options = 0;
        stream_data.reserved = 0;
        stream_data.mon_stream_size = 0;

        hr = IStream_Write(stream, &stream_data, sizeof(stream_data), NULL);
        IStream_Release(stream);
    }

    return hr;
}

/* write a string to a stream, preceded by its length */
static HRESULT STREAM_WriteString( IStream *stm, LPCWSTR string )
{
    HRESULT r;
    LPSTR str;
    DWORD len = 0;

    if( string )
        len = WideCharToMultiByte( CP_ACP, 0, string, -1, NULL, 0, NULL, NULL);
    r = IStream_Write( stm, &len, sizeof(len), NULL);
    if( FAILED( r ) )
        return r;
    if(len == 0)
        return r;
    str = CoTaskMemAlloc( len );
    WideCharToMultiByte( CP_ACP, 0, string, -1, str, len, NULL, NULL);
    r = IStream_Write( stm, str, len, NULL);
    CoTaskMemFree( str );
    return r;
}

/* read a string preceded by its length from a stream */
static HRESULT STREAM_ReadString( IStream *stm, LPWSTR *string )
{
    HRESULT r;
    DWORD len, count = 0;
    LPSTR str;
    LPWSTR wstr;

    r = IStream_Read( stm, &len, sizeof(len), &count );
    if( FAILED( r ) )
        return r;
    if( count != sizeof(len) )
        return E_OUTOFMEMORY;

    TRACE("%ld bytes\n",len);

    str = CoTaskMemAlloc( len );
    if( !str )
        return E_OUTOFMEMORY;
    count = 0;
    r = IStream_Read( stm, str, len, &count );
    if( FAILED( r ) )
    {
        CoTaskMemFree( str );
        return r;
    }
    if( count != len )
    {
        CoTaskMemFree( str );
        return E_OUTOFMEMORY;
    }

    TRACE("Read string %s\n",debugstr_an(str,len));

    len = MultiByteToWideChar( CP_ACP, 0, str, count, NULL, 0 );
    wstr = CoTaskMemAlloc( (len + 1)*sizeof (WCHAR) );
    if( wstr )
    {
         MultiByteToWideChar( CP_ACP, 0, str, count, wstr, len );
         wstr[len] = 0;
    }
    CoTaskMemFree( str );

    *string = wstr;

    return r;
}


static HRESULT STORAGE_WriteCompObj( LPSTORAGE pstg, CLSID *clsid,
    LPCWSTR lpszUserType, LPCWSTR szClipName, LPCWSTR szProgIDName )
{
    IStream *pstm;
    HRESULT r = S_OK;

    static const BYTE unknown1[12] =
       { 0x01, 0x00, 0xFE, 0xFF, 0x03, 0x0A, 0x00, 0x00,
         0xFF, 0xFF, 0xFF, 0xFF};
    static const BYTE unknown2[16] =
       { 0xF4, 0x39, 0xB2, 0x71, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    TRACE("%p %s %s %s %s\n", pstg, debugstr_guid(clsid),
           debugstr_w(lpszUserType), debugstr_w(szClipName),
           debugstr_w(szProgIDName));

    /*  Create a CompObj stream */
    r = IStorage_CreateStream(pstg, L"\1CompObj",
        STGM_CREATE | STGM_WRITE  | STGM_SHARE_EXCLUSIVE, 0, 0, &pstm );
    if( FAILED (r) )
        return r;

    /* Write CompObj Structure to stream */
    r = IStream_Write(pstm, unknown1, sizeof(unknown1), NULL);

    if( SUCCEEDED( r ) )
        r = WriteClassStm( pstm, clsid );

    if( SUCCEEDED( r ) )
        r = STREAM_WriteString( pstm, lpszUserType );
    if( SUCCEEDED( r ) )
        r = STREAM_WriteString( pstm, szClipName );
    if( SUCCEEDED( r ) )
        r = STREAM_WriteString( pstm, szProgIDName );
    if( SUCCEEDED( r ) )
        r = IStream_Write(pstm, unknown2, sizeof(unknown2), NULL);

    IStream_Release( pstm );

    return r;
}

/***********************************************************************
 *               WriteFmtUserTypeStg (OLE32.@)
 */
HRESULT WINAPI WriteFmtUserTypeStg(
	  LPSTORAGE pstg, CLIPFORMAT cf, LPOLESTR lpszUserType)
{
    STATSTG stat;
    HRESULT r;
    WCHAR szwClipName[0x40];
    CLSID clsid;
    LPWSTR wstrProgID = NULL;
    DWORD n;

    TRACE("(%p,%x,%s)\n",pstg,cf,debugstr_w(lpszUserType));

    /* get the clipboard format name */
    if( cf )
    {
        n = GetClipboardFormatNameW(cf, szwClipName, ARRAY_SIZE(szwClipName));
        szwClipName[n]=0;
    }

    TRACE("Clipboard name is %s\n", debugstr_w(szwClipName));

    r = IStorage_Stat(pstg, &stat, STATFLAG_NONAME);
    if(SUCCEEDED(r))
        clsid = stat.clsid;
    else
        clsid = CLSID_NULL;

    ProgIDFromCLSID(&clsid, &wstrProgID);

    TRACE("progid is %s\n",debugstr_w(wstrProgID));

    r = STORAGE_WriteCompObj( pstg, &clsid, lpszUserType,
            cf ? szwClipName : NULL, wstrProgID );

    CoTaskMemFree(wstrProgID);

    return r;
}


/******************************************************************************
 *              ReadFmtUserTypeStg        [OLE32.@]
 */
HRESULT WINAPI ReadFmtUserTypeStg (LPSTORAGE pstg, CLIPFORMAT* pcf, LPOLESTR* lplpszUserType)
{
    HRESULT r;
    IStream *stm = 0;
    unsigned char unknown1[12];
    unsigned char unknown2[16];
    DWORD count;
    LPWSTR szProgIDName = NULL, szCLSIDName = NULL, szOleTypeName = NULL;
    CLSID clsid;

    TRACE("(%p,%p,%p)\n", pstg, pcf, lplpszUserType);

    r = IStorage_OpenStream( pstg, L"\1CompObj", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stm );
    if( FAILED ( r ) )
    {
        WARN("Failed to open stream r = %#lx\n", r);
        return r;
    }

    /* read the various parts of the structure */
    r = IStream_Read( stm, unknown1, sizeof(unknown1), &count );
    if( FAILED( r ) || ( count != sizeof(unknown1) ) )
        goto end;
    r = ReadClassStm( stm, &clsid );
    if( FAILED( r ) )
        goto end;

    r = STREAM_ReadString( stm, &szCLSIDName );
    if( FAILED( r ) )
        goto end;

    r = STREAM_ReadString( stm, &szOleTypeName );
    if( FAILED( r ) )
        goto end;

    r = STREAM_ReadString( stm, &szProgIDName );
    if( FAILED( r ) )
        goto end;

    r = IStream_Read( stm, unknown2, sizeof(unknown2), &count );
    if( FAILED( r ) || ( count != sizeof(unknown2) ) )
        goto end;

    /* ok, success... now we just need to store what we found */
    if( pcf )
        *pcf = RegisterClipboardFormatW( szOleTypeName );

    if( lplpszUserType )
    {
        *lplpszUserType = szCLSIDName;
        szCLSIDName = NULL;
    }

end:
    CoTaskMemFree( szCLSIDName );
    CoTaskMemFree( szOleTypeName );
    CoTaskMemFree( szProgIDName );
    IStream_Release( stm );

    return r;
}

/************************************************************************
 * OleConvert Functions
 ***********************************************************************/

#define OLESTREAM_ID 0x501
#define OLESTREAM_MAX_STR_LEN 255

/* OLESTREAM memory structure to use for Get and Put Routines */
typedef struct
{
    DWORD dwOleID;
    DWORD dwTypeID;
    DWORD dwOleTypeNameLength;
    CHAR  strOleTypeName[OLESTREAM_MAX_STR_LEN];
    CHAR  *pstrOleObjFileName;
    DWORD dwOleObjFileNameLength;
    DWORD dwMetaFileWidth;
    DWORD dwMetaFileHeight;
    CHAR  strUnknown[8]; /* don't know what this 8 byte information in OLE stream is. */
    DWORD dwDataLength;
    BYTE *pData;
} OLECONVERT_OLESTREAM_DATA;

/* CompObj Stream structure */
typedef struct
{
    BYTE byUnknown1[12];
    CLSID clsid;
    DWORD dwCLSIDNameLength;
    CHAR strCLSIDName[OLESTREAM_MAX_STR_LEN];
    DWORD dwOleTypeNameLength;
    CHAR strOleTypeName[OLESTREAM_MAX_STR_LEN];
    DWORD dwProgIDNameLength;
    CHAR strProgIDName[OLESTREAM_MAX_STR_LEN];
    BYTE byUnknown2[16];
} OLECONVERT_ISTORAGE_COMPOBJ;

/* Ole Presentation Stream structure */
typedef struct
{
    BYTE byUnknown1[28];
    DWORD dwExtentX;
    DWORD dwExtentY;
    DWORD dwSize;
    BYTE *pData;
} OLECONVERT_ISTORAGE_OLEPRES;


/*************************************************************************
 * OLECONVERT_LoadOLE10 [Internal]
 *
 * Loads the OLE10 STREAM to memory
 *
 * PARAMS
 *     pOleStream   [I] The OLESTREAM
 *     pData        [I] Data Structure for the OLESTREAM Data
 *
 * RETURNS
 *     Success:  S_OK
 *     Failure:  CONVERT10_E_OLESTREAM_GET for invalid Get
 *               CONVERT10_E_OLESTREAM_FMT if the OLEID is invalid
 *
 * NOTES
 *     This function is used by OleConvertOLESTREAMToIStorage only.
 *
 *     Memory allocated for pData must be freed by the caller
 */
static HRESULT OLECONVERT_LoadOLE10(LPOLESTREAM pOleStream, OLECONVERT_OLESTREAM_DATA *pData, BOOL bStream1)
{
	DWORD dwSize;
	HRESULT hRes = S_OK;
	int nTryCnt=0;
	int max_try = 6;

	pData->pData = NULL;
	pData->pstrOleObjFileName = NULL;

	for( nTryCnt=0;nTryCnt < max_try; nTryCnt++)
	{
	/* Get the OleID */
	dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwOleID), sizeof(pData->dwOleID));
	if(dwSize != sizeof(pData->dwOleID))
	{
		hRes = CONVERT10_E_OLESTREAM_GET;
	}
	else if(pData->dwOleID != OLESTREAM_ID)
	{
		hRes = CONVERT10_E_OLESTREAM_FMT;
	}
		else
		{
			hRes = S_OK;
			break;
		}
	}

	if(hRes == S_OK)
	{
		/* Get the TypeID... more info needed for this field */
		dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwTypeID), sizeof(pData->dwTypeID));
		if(dwSize != sizeof(pData->dwTypeID))
		{
			hRes = CONVERT10_E_OLESTREAM_GET;
		}
	}
	if(hRes == S_OK)
	{
		if(pData->dwTypeID != 0)
		{
			/* Get the length of the OleTypeName */
			dwSize = pOleStream->lpstbl->Get(pOleStream, (void *) &(pData->dwOleTypeNameLength), sizeof(pData->dwOleTypeNameLength));
			if(dwSize != sizeof(pData->dwOleTypeNameLength))
			{
				hRes = CONVERT10_E_OLESTREAM_GET;
			}

			if(hRes == S_OK)
			{
				if(pData->dwOleTypeNameLength > 0)
				{
					/* Get the OleTypeName */
					dwSize = pOleStream->lpstbl->Get(pOleStream, pData->strOleTypeName, pData->dwOleTypeNameLength);
					if(dwSize != pData->dwOleTypeNameLength)
					{
						hRes = CONVERT10_E_OLESTREAM_GET;
					}
				}
			}
			if(bStream1)
			{
				dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwOleObjFileNameLength), sizeof(pData->dwOleObjFileNameLength));
				if(dwSize != sizeof(pData->dwOleObjFileNameLength))
				{
					hRes = CONVERT10_E_OLESTREAM_GET;
				}
			if(hRes == S_OK)
			{
					if(pData->dwOleObjFileNameLength < 1) /* there is no file name exist */
						pData->dwOleObjFileNameLength = sizeof(pData->dwOleObjFileNameLength);
					pData->pstrOleObjFileName = HeapAlloc(GetProcessHeap(), 0, pData->dwOleObjFileNameLength);
					if(pData->pstrOleObjFileName)
					{
						dwSize = pOleStream->lpstbl->Get(pOleStream, pData->pstrOleObjFileName, pData->dwOleObjFileNameLength);
						if(dwSize != pData->dwOleObjFileNameLength)
						{
							hRes = CONVERT10_E_OLESTREAM_GET;
						}
					}
					else
						hRes = CONVERT10_E_OLESTREAM_GET;
				}
			}
			else
			{
				/* Get the Width of the Metafile */
				dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwMetaFileWidth), sizeof(pData->dwMetaFileWidth));
				if(dwSize != sizeof(pData->dwMetaFileWidth))
				{
					hRes = CONVERT10_E_OLESTREAM_GET;
				}
			if(hRes == S_OK)
			{
				/* Get the Height of the Metafile */
				dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwMetaFileHeight), sizeof(pData->dwMetaFileHeight));
				if(dwSize != sizeof(pData->dwMetaFileHeight))
				{
					hRes = CONVERT10_E_OLESTREAM_GET;
				}
			}
			}
			if(hRes == S_OK)
			{
				/* Get the Length of the Data */
				dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)&(pData->dwDataLength), sizeof(pData->dwDataLength));
				if(dwSize != sizeof(pData->dwDataLength))
				{
					hRes = CONVERT10_E_OLESTREAM_GET;
				}
			}

			if(hRes == S_OK) /* I don't know what this 8 byte information is. We have to figure out */
			{
				if(!bStream1) /* if it is a second OLE stream data */
				{
					pData->dwDataLength -= 8;
					dwSize = pOleStream->lpstbl->Get(pOleStream, pData->strUnknown, sizeof(pData->strUnknown));
					if(dwSize != sizeof(pData->strUnknown))
					{
						hRes = CONVERT10_E_OLESTREAM_GET;
					}
				}
			}
			if(hRes == S_OK)
			{
				if(pData->dwDataLength > 0)
				{
					pData->pData = HeapAlloc(GetProcessHeap(),0,pData->dwDataLength);

					/* Get Data (ex. IStorage, Metafile, or BMP) */
					if(pData->pData)
					{
						dwSize = pOleStream->lpstbl->Get(pOleStream, (void *)pData->pData, pData->dwDataLength);
						if(dwSize != pData->dwDataLength)
						{
							hRes = CONVERT10_E_OLESTREAM_GET;
						}
					}
					else
					{
						hRes = CONVERT10_E_OLESTREAM_GET;
					}
				}
			}
		}
	}
	return hRes;
}

/*************************************************************************
 * OLECONVERT_SaveOLE10 [Internal]
 *
 * Saves the OLE10 STREAM From memory
 *
 * PARAMS
 *     pData        [I] Data Structure for the OLESTREAM Data
 *     pOleStream   [I] The OLESTREAM to save
 *
 * RETURNS
 *     Success:  S_OK
 *     Failure:  CONVERT10_E_OLESTREAM_PUT for invalid Put
 *
 * NOTES
 *     This function is used by OleConvertIStorageToOLESTREAM only.
 *
 */
static HRESULT OLECONVERT_SaveOLE10(OLECONVERT_OLESTREAM_DATA *pData, LPOLESTREAM pOleStream)
{
    DWORD dwSize;
    HRESULT hRes = S_OK;


   /* Set the OleID */
    dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwOleID), sizeof(pData->dwOleID));
    if(dwSize != sizeof(pData->dwOleID))
    {
        hRes = CONVERT10_E_OLESTREAM_PUT;
    }

    if(hRes == S_OK)
    {
        /* Set the TypeID */
        dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwTypeID), sizeof(pData->dwTypeID));
        if(dwSize != sizeof(pData->dwTypeID))
        {
            hRes = CONVERT10_E_OLESTREAM_PUT;
        }
    }

    if(pData->dwOleID == OLESTREAM_ID && pData->dwTypeID != 0 && hRes == S_OK)
    {
        /* Set the Length of the OleTypeName */
        dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwOleTypeNameLength), sizeof(pData->dwOleTypeNameLength));
        if(dwSize != sizeof(pData->dwOleTypeNameLength))
        {
            hRes = CONVERT10_E_OLESTREAM_PUT;
        }

        if(hRes == S_OK)
        {
            if(pData->dwOleTypeNameLength > 0)
            {
                /* Set the OleTypeName */
                dwSize = pOleStream->lpstbl->Put(pOleStream, pData->strOleTypeName, pData->dwOleTypeNameLength);
                if(dwSize != pData->dwOleTypeNameLength)
                {
                    hRes = CONVERT10_E_OLESTREAM_PUT;
                }
            }
        }

        if(hRes == S_OK)
        {
            /* Set the width of the Metafile */
            dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwMetaFileWidth), sizeof(pData->dwMetaFileWidth));
            if(dwSize != sizeof(pData->dwMetaFileWidth))
            {
                hRes = CONVERT10_E_OLESTREAM_PUT;
            }
        }

        if(hRes == S_OK)
        {
            /* Set the height of the Metafile */
            dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwMetaFileHeight), sizeof(pData->dwMetaFileHeight));
            if(dwSize != sizeof(pData->dwMetaFileHeight))
            {
                hRes = CONVERT10_E_OLESTREAM_PUT;
            }
        }

        if(hRes == S_OK)
        {
            /* Set the length of the Data */
            dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)&(pData->dwDataLength), sizeof(pData->dwDataLength));
            if(dwSize != sizeof(pData->dwDataLength))
            {
                hRes = CONVERT10_E_OLESTREAM_PUT;
            }
        }

        if(hRes == S_OK)
        {
            if(pData->dwDataLength > 0)
            {
                /* Set the Data (eg. IStorage, Metafile, Bitmap) */
                dwSize = pOleStream->lpstbl->Put(pOleStream, (void *)  pData->pData, pData->dwDataLength);
                if(dwSize != pData->dwDataLength)
                {
                    hRes = CONVERT10_E_OLESTREAM_PUT;
                }
            }
        }
    }
    return hRes;
}

/*************************************************************************
 * OLECONVERT_GetOLE20FromOLE10[Internal]
 *
 * This function copies OLE10 Data (the IStorage in the OLESTREAM) to disk,
 * opens it, and copies the content to the dest IStorage for
 * OleConvertOLESTREAMToIStorage
 *
 *
 * PARAMS
 *     pDestStorage  [I] The IStorage to copy the data to
 *     pBuffer       [I] Buffer that contains the IStorage from the OLESTREAM
 *     nBufferLength [I] The size of the buffer
 *
 * RETURNS
 *     Nothing
 *
 * NOTES
 *
 *
 */
static void OLECONVERT_GetOLE20FromOLE10(LPSTORAGE pDestStorage, const BYTE *pBuffer, DWORD nBufferLength)
{
    HRESULT hRes;
    HANDLE hFile;
    IStorage *pTempStorage;
    DWORD dwNumOfBytesWritten;
    WCHAR wstrTempDir[MAX_PATH], wstrTempFile[MAX_PATH];

    /* Create a temp File */
    GetTempPathW(MAX_PATH, wstrTempDir);
    GetTempFileNameW(wstrTempDir, L"sis", 0, wstrTempFile);
    hFile = CreateFileW(wstrTempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if(hFile != INVALID_HANDLE_VALUE)
    {
        /* Write IStorage Data to File */
        WriteFile(hFile, pBuffer, nBufferLength, &dwNumOfBytesWritten, NULL);
        CloseHandle(hFile);

        /* Open and copy temp storage to the Dest Storage */
        hRes = StgOpenStorage(wstrTempFile, NULL, STGM_READ, NULL, 0, &pTempStorage);
        if(hRes == S_OK)
        {
            hRes = IStorage_CopyTo(pTempStorage, 0, NULL, NULL, pDestStorage);
            IStorage_Release(pTempStorage);
        }
        DeleteFileW(wstrTempFile);
    }
}


/*************************************************************************
 * OLECONVERT_WriteOLE20ToBuffer [Internal]
 *
 * Saves the OLE10 STREAM From memory
 *
 * PARAMS
 *     pStorage  [I] The Src IStorage to copy
 *     pData     [I] The Dest Memory to write to.
 *
 * RETURNS
 *     The size in bytes allocated for pData
 *
 * NOTES
 *     Memory allocated for pData must be freed by the caller
 *
 *     Used by OleConvertIStorageToOLESTREAM only.
 *
 */
static DWORD OLECONVERT_WriteOLE20ToBuffer(LPSTORAGE pStorage, BYTE **pData)
{
    HANDLE hFile;
    HRESULT hRes;
    DWORD nDataLength = 0;
    IStorage *pTempStorage;
    WCHAR wstrTempDir[MAX_PATH], wstrTempFile[MAX_PATH];

    *pData = NULL;

    /* Create temp Storage */
    GetTempPathW(MAX_PATH, wstrTempDir);
    GetTempFileNameW(wstrTempDir, L"sis", 0, wstrTempFile);
    hRes = StgCreateDocfile(wstrTempFile, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &pTempStorage);

    if(hRes == S_OK)
    {
        /* Copy Src Storage to the Temp Storage */
        IStorage_CopyTo(pStorage, 0, NULL, NULL, pTempStorage);
        IStorage_Release(pTempStorage);

        /* Open Temp Storage as a file and copy to memory */
        hFile = CreateFileW(wstrTempFile, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if(hFile != INVALID_HANDLE_VALUE)
        {
            nDataLength = GetFileSize(hFile, NULL);
            *pData = HeapAlloc(GetProcessHeap(),0,nDataLength);
            ReadFile(hFile, *pData, nDataLength, &nDataLength, 0);
            CloseHandle(hFile);
        }
        DeleteFileW(wstrTempFile);
    }
    return nDataLength;
}

/*************************************************************************
 * OLECONVERT_CreateCompObjStream [Internal]
 *
 * Creates a "\001CompObj" is the destination IStorage if necessary.
 *
 * PARAMS
 *     pStorage       [I] The dest IStorage to create the CompObj Stream
 *                        if necessary.
 *     strOleTypeName [I] The ProgID
 *
 * RETURNS
 *     Success:  S_OK
 *     Failure:  REGDB_E_CLASSNOTREG if cannot reconstruct the stream
 *
 * NOTES
 *     This function is used by OleConvertOLESTREAMToIStorage only.
 *
 *     The stream data is stored in the OLESTREAM and there should be
 *     no need to recreate the stream.  If the stream is manually
 *     deleted it will attempt to create it by querying the registry.
 *
 *
 */
HRESULT OLECONVERT_CreateCompObjStream(LPSTORAGE pStorage, LPCSTR strOleTypeName)
{
    IStream *pStream;
    HRESULT hStorageRes, hRes = S_OK;
    OLECONVERT_ISTORAGE_COMPOBJ IStorageCompObj;
    WCHAR bufferW[OLESTREAM_MAX_STR_LEN];

    static const BYTE pCompObjUnknown1[] = {0x01, 0x00, 0xFE, 0xFF, 0x03, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    static const BYTE pCompObjUnknown2[] = {0xF4, 0x39, 0xB2, 0x71};

    /* Initialize the CompObj structure */
    memset(&IStorageCompObj, 0, sizeof(IStorageCompObj));
    memcpy(IStorageCompObj.byUnknown1, pCompObjUnknown1, sizeof(pCompObjUnknown1));
    memcpy(IStorageCompObj.byUnknown2, pCompObjUnknown2, sizeof(pCompObjUnknown2));


    /*  Create a CompObj stream if it doesn't exist */
    hStorageRes = IStorage_CreateStream(pStorage, L"\1CompObj",
        STGM_WRITE  | STGM_SHARE_EXCLUSIVE, 0, 0, &pStream );
    if(hStorageRes == S_OK)
    {
        /* copy the OleTypeName to the compobj struct */
        IStorageCompObj.dwOleTypeNameLength = strlen(strOleTypeName)+1;
        strcpy(IStorageCompObj.strOleTypeName, strOleTypeName);

        /* copy the OleTypeName to the compobj struct */
        /* Note: in the test made, these were Identical      */
        IStorageCompObj.dwProgIDNameLength = strlen(strOleTypeName)+1;
        strcpy(IStorageCompObj.strProgIDName, strOleTypeName);

        /* Get the CLSID */
        MultiByteToWideChar( CP_ACP, 0, IStorageCompObj.strProgIDName, -1,
                             bufferW, OLESTREAM_MAX_STR_LEN );
        hRes = CLSIDFromProgID(bufferW, &(IStorageCompObj.clsid));

        if(hRes == S_OK)
        {
            HKEY hKey;
            LONG hErr;
            /* Get the CLSID Default Name from the Registry */
            hErr = open_classes_key(HKEY_CLASSES_ROOT, bufferW, MAXIMUM_ALLOWED, &hKey);
            if(hErr == ERROR_SUCCESS)
            {
                char strTemp[OLESTREAM_MAX_STR_LEN];
                IStorageCompObj.dwCLSIDNameLength = OLESTREAM_MAX_STR_LEN;
                hErr = RegQueryValueA(hKey, NULL, strTemp, (LONG*) &(IStorageCompObj.dwCLSIDNameLength));
                if(hErr == ERROR_SUCCESS)
                {
                    strcpy(IStorageCompObj.strCLSIDName, strTemp);
                }
                RegCloseKey(hKey);
            }
        }

        /* Write CompObj Structure to stream */
        hRes = IStream_Write(pStream, IStorageCompObj.byUnknown1, sizeof(IStorageCompObj.byUnknown1), NULL);

        WriteClassStm(pStream,&(IStorageCompObj.clsid));

        hRes = IStream_Write(pStream, &(IStorageCompObj.dwCLSIDNameLength), sizeof(IStorageCompObj.dwCLSIDNameLength), NULL);
        if(IStorageCompObj.dwCLSIDNameLength > 0)
        {
            hRes = IStream_Write(pStream, IStorageCompObj.strCLSIDName, IStorageCompObj.dwCLSIDNameLength, NULL);
        }
        hRes = IStream_Write(pStream, &(IStorageCompObj.dwOleTypeNameLength) , sizeof(IStorageCompObj.dwOleTypeNameLength), NULL);
        if(IStorageCompObj.dwOleTypeNameLength > 0)
        {
            hRes = IStream_Write(pStream, IStorageCompObj.strOleTypeName , IStorageCompObj.dwOleTypeNameLength, NULL);
        }
        hRes = IStream_Write(pStream, &(IStorageCompObj.dwProgIDNameLength) , sizeof(IStorageCompObj.dwProgIDNameLength), NULL);
        if(IStorageCompObj.dwProgIDNameLength > 0)
        {
            hRes = IStream_Write(pStream, IStorageCompObj.strProgIDName , IStorageCompObj.dwProgIDNameLength, NULL);
        }
        hRes = IStream_Write(pStream, IStorageCompObj.byUnknown2 , sizeof(IStorageCompObj.byUnknown2), NULL);
        IStream_Release(pStream);
    }
    return hRes;
}


/*************************************************************************
 * OLECONVERT_CreateOlePresStream[Internal]
 *
 * Creates the "\002OlePres000" Stream with the Metafile data
 *
 * PARAMS
 *     pStorage     [I] The dest IStorage to create \002OLEPres000 stream in.
 *     dwExtentX    [I] Width of the Metafile
 *     dwExtentY    [I] Height of the Metafile
 *     pData        [I] Metafile data
 *     dwDataLength [I] Size of the Metafile data
 *
 * RETURNS
 *     Success:  S_OK
 *     Failure:  CONVERT10_E_OLESTREAM_PUT for invalid Put
 *
 * NOTES
 *     This function is used by OleConvertOLESTREAMToIStorage only.
 *
 */
static void OLECONVERT_CreateOlePresStream(LPSTORAGE pStorage, DWORD dwExtentX, DWORD dwExtentY , BYTE *pData, DWORD dwDataLength)
{
    HRESULT hRes;
    IStream *pStream;
    static const BYTE pOlePresStreamHeader[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    static const BYTE pOlePresStreamHeaderEmpty[] =
    {
        0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    /* Create the OlePres000 Stream */
    hRes = IStorage_CreateStream(pStorage, L"\2OlePres000",
        STGM_CREATE | STGM_WRITE  | STGM_SHARE_EXCLUSIVE, 0, 0, &pStream );

    if(hRes == S_OK)
    {
        DWORD nHeaderSize;
        OLECONVERT_ISTORAGE_OLEPRES OlePres;

        memset(&OlePres, 0, sizeof(OlePres));
        /* Do we have any metafile data to save */
        if(dwDataLength > 0)
        {
            memcpy(OlePres.byUnknown1, pOlePresStreamHeader, sizeof(pOlePresStreamHeader));
            nHeaderSize = sizeof(pOlePresStreamHeader);
        }
        else
        {
            memcpy(OlePres.byUnknown1, pOlePresStreamHeaderEmpty, sizeof(pOlePresStreamHeaderEmpty));
            nHeaderSize = sizeof(pOlePresStreamHeaderEmpty);
        }
        /* Set width and height of the metafile */
        OlePres.dwExtentX = dwExtentX;
        OlePres.dwExtentY = -dwExtentY;

        /* Set Data and Length */
        if(dwDataLength > sizeof(METAFILEPICT16))
        {
            OlePres.dwSize = dwDataLength - sizeof(METAFILEPICT16);
            OlePres.pData = &(pData[8]);
        }
        /* Save OlePres000 Data to Stream */
        hRes = IStream_Write(pStream, OlePres.byUnknown1, nHeaderSize, NULL);
        hRes = IStream_Write(pStream, &(OlePres.dwExtentX), sizeof(OlePres.dwExtentX), NULL);
        hRes = IStream_Write(pStream, &(OlePres.dwExtentY), sizeof(OlePres.dwExtentY), NULL);
        hRes = IStream_Write(pStream, &(OlePres.dwSize), sizeof(OlePres.dwSize), NULL);
        if(OlePres.dwSize > 0)
        {
            hRes = IStream_Write(pStream, OlePres.pData, OlePres.dwSize, NULL);
        }
        IStream_Release(pStream);
    }
}

/*************************************************************************
 * OLECONVERT_CreateOle10NativeStream [Internal]
 *
 * Creates the "\001Ole10Native" Stream (should contain a BMP)
 *
 * PARAMS
 *     pStorage     [I] Dest storage to create the stream in
 *     pData        [I] Ole10 Native Data (ex. bmp)
 *     dwDataLength [I] Size of the Ole10 Native Data
 *
 * RETURNS
 *     Nothing
 *
 * NOTES
 *     This function is used by OleConvertOLESTREAMToIStorage only.
 *
 *     Might need to verify the data and return appropriate error message
 *
 */
static void OLECONVERT_CreateOle10NativeStream(LPSTORAGE pStorage, const BYTE *pData, DWORD dwDataLength)
{
    HRESULT hRes;
    IStream *pStream;

    /* Create the Ole10Native Stream */
    hRes = IStorage_CreateStream(pStorage, L"\1Ole10Native",
        STGM_CREATE | STGM_WRITE  | STGM_SHARE_EXCLUSIVE, 0, 0, &pStream );

    if(hRes == S_OK)
    {
        /* Write info to stream */
        hRes = IStream_Write(pStream, &dwDataLength, sizeof(dwDataLength), NULL);
        hRes = IStream_Write(pStream, pData, dwDataLength, NULL);
        IStream_Release(pStream);
    }

}

/*************************************************************************
 * OLECONVERT_GetOLE10ProgID [Internal]
 *
 * Finds the ProgID (or OleTypeID) from the IStorage
 *
 * PARAMS
 *     pStorage        [I] The Src IStorage to get the ProgID
 *     strProgID       [I] the ProgID string to get
 *     dwSize          [I] the size of the string
 *
 * RETURNS
 *     Success:  S_OK
 *     Failure:  REGDB_E_CLASSNOTREG if cannot reconstruct the stream
 *
 * NOTES
 *     This function is used by OleConvertIStorageToOLESTREAM only.
 *
 *
 */
static HRESULT OLECONVERT_GetOLE10ProgID(LPSTORAGE pStorage, char *strProgID, DWORD *dwSize)
{
    HRESULT hRes;
    IStream *pStream;
    LARGE_INTEGER iSeekPos;
    OLECONVERT_ISTORAGE_COMPOBJ CompObj;

    /* Open the CompObj Stream */
    hRes = IStorage_OpenStream(pStorage, L"\1CompObj", NULL,
        STGM_READ  | STGM_SHARE_EXCLUSIVE, 0, &pStream );
    if(hRes == S_OK)
    {

        /*Get the OleType from the CompObj Stream */
        iSeekPos.LowPart = sizeof(CompObj.byUnknown1) + sizeof(CompObj.clsid);
        iSeekPos.HighPart = 0;

        IStream_Seek(pStream, iSeekPos, STREAM_SEEK_SET, NULL);
        IStream_Read(pStream, &CompObj.dwCLSIDNameLength, sizeof(CompObj.dwCLSIDNameLength), NULL);
        iSeekPos.LowPart = CompObj.dwCLSIDNameLength;
        IStream_Seek(pStream, iSeekPos, STREAM_SEEK_CUR , NULL);
        IStream_Read(pStream, &CompObj.dwOleTypeNameLength, sizeof(CompObj.dwOleTypeNameLength), NULL);
        iSeekPos.LowPart = CompObj.dwOleTypeNameLength;
        IStream_Seek(pStream, iSeekPos, STREAM_SEEK_CUR , NULL);

        IStream_Read(pStream, dwSize, sizeof(*dwSize), NULL);
        if(*dwSize > 0)
        {
            IStream_Read(pStream, strProgID, *dwSize, NULL);
        }
        IStream_Release(pStream);
    }
    else
    {
        STATSTG stat;
        LPOLESTR wstrProgID;

        /* Get the OleType from the registry */
        REFCLSID clsid = &(stat.clsid);
        IStorage_Stat(pStorage, &stat, STATFLAG_NONAME);
        hRes = ProgIDFromCLSID(clsid, &wstrProgID);
        if(hRes == S_OK)
        {
            *dwSize = WideCharToMultiByte(CP_ACP, 0, wstrProgID, -1, strProgID, *dwSize, NULL, FALSE);
            CoTaskMemFree(wstrProgID);
        }

    }
    return hRes;
}

/*************************************************************************
 * OLECONVERT_GetOle10PresData [Internal]
 *
 * Converts IStorage "/001Ole10Native" stream to a OLE10 Stream
 *
 * PARAMS
 *     pStorage     [I] Src IStroage
 *     pOleStream   [I] Dest OleStream Mem Struct
 *
 * RETURNS
 *     Nothing
 *
 * NOTES
 *     This function is used by OleConvertIStorageToOLESTREAM only.
 *
 *     Memory allocated for pData must be freed by the caller
 *
 *
 */
static void OLECONVERT_GetOle10PresData(LPSTORAGE pStorage, OLECONVERT_OLESTREAM_DATA *pOleStreamData)
{

    HRESULT hRes;
    IStream *pStream;

    /* Initialize Default data for OLESTREAM */
    pOleStreamData[0].dwOleID = OLESTREAM_ID;
    pOleStreamData[0].dwTypeID = 2;
    pOleStreamData[1].dwOleID = OLESTREAM_ID;
    pOleStreamData[1].dwTypeID = 0;
    pOleStreamData[0].dwMetaFileWidth = 0;
    pOleStreamData[0].dwMetaFileHeight = 0;
    pOleStreamData[0].pData = NULL;
    pOleStreamData[1].pData = NULL;

    /* Open Ole10Native Stream */
    hRes = IStorage_OpenStream(pStorage, L"\1Ole10Native", NULL,
        STGM_READ  | STGM_SHARE_EXCLUSIVE, 0, &pStream );
    if(hRes == S_OK)
    {

        /* Read Size and Data */
        IStream_Read(pStream, &(pOleStreamData->dwDataLength), sizeof(pOleStreamData->dwDataLength), NULL);
        if(pOleStreamData->dwDataLength > 0)
        {
            pOleStreamData->pData = HeapAlloc(GetProcessHeap(),0,pOleStreamData->dwDataLength);
            IStream_Read(pStream, pOleStreamData->pData, pOleStreamData->dwDataLength, NULL);
        }
        IStream_Release(pStream);
    }

}


/*************************************************************************
 * OLECONVERT_GetOle20PresData[Internal]
 *
 * Converts IStorage "/002OlePres000" stream to a OLE10 Stream
 *
 * PARAMS
 *     pStorage         [I] Src IStroage
 *     pOleStreamData   [I] Dest OleStream Mem Struct
 *
 * RETURNS
 *     Nothing
 *
 * NOTES
 *     This function is used by OleConvertIStorageToOLESTREAM only.
 *
 *     Memory allocated for pData must be freed by the caller
 */
static void OLECONVERT_GetOle20PresData(LPSTORAGE pStorage, OLECONVERT_OLESTREAM_DATA *pOleStreamData)
{
    HRESULT hRes;
    IStream *pStream;
    OLECONVERT_ISTORAGE_OLEPRES olePress;

    /* Initialize Default data for OLESTREAM */
    pOleStreamData[0].dwOleID = OLESTREAM_ID;
    pOleStreamData[0].dwTypeID = 2;
    pOleStreamData[0].dwMetaFileWidth = 0;
    pOleStreamData[0].dwMetaFileHeight = 0;
    pOleStreamData[0].dwDataLength = OLECONVERT_WriteOLE20ToBuffer(pStorage, &(pOleStreamData[0].pData));
    pOleStreamData[1].dwOleID = OLESTREAM_ID;
    pOleStreamData[1].dwTypeID = 0;
    pOleStreamData[1].dwOleTypeNameLength = 0;
    pOleStreamData[1].strOleTypeName[0] = 0;
    pOleStreamData[1].dwMetaFileWidth = 0;
    pOleStreamData[1].dwMetaFileHeight = 0;
    pOleStreamData[1].pData = NULL;
    pOleStreamData[1].dwDataLength = 0;


    /* Open OlePress000 stream */
    hRes = IStorage_OpenStream(pStorage, L"\2OlePres000", NULL,
        STGM_READ  | STGM_SHARE_EXCLUSIVE, 0, &pStream );
    if(hRes == S_OK)
    {
        LARGE_INTEGER iSeekPos;
        METAFILEPICT16 MetaFilePict;
        static const char strMetafilePictName[] = "METAFILEPICT";

        /* Set the TypeID for a Metafile */
        pOleStreamData[1].dwTypeID = 5;

        /* Set the OleTypeName to Metafile */
        pOleStreamData[1].dwOleTypeNameLength = strlen(strMetafilePictName) +1;
        strcpy(pOleStreamData[1].strOleTypeName, strMetafilePictName);

        iSeekPos.HighPart = 0;
        iSeekPos.LowPart = sizeof(olePress.byUnknown1);

        /* Get Presentation Data */
        IStream_Seek(pStream, iSeekPos, STREAM_SEEK_SET, NULL);
        IStream_Read(pStream, &(olePress.dwExtentX), sizeof(olePress.dwExtentX), NULL);
        IStream_Read(pStream, &(olePress.dwExtentY), sizeof(olePress.dwExtentY), NULL);
        IStream_Read(pStream, &(olePress.dwSize), sizeof(olePress.dwSize), NULL);

        /*Set width and Height */
        pOleStreamData[1].dwMetaFileWidth = olePress.dwExtentX;
        pOleStreamData[1].dwMetaFileHeight = -olePress.dwExtentY;
        if(olePress.dwSize > 0)
        {
            /* Set Length */
            pOleStreamData[1].dwDataLength  = olePress.dwSize + sizeof(METAFILEPICT16);

            /* Set MetaFilePict struct */
            MetaFilePict.mm = 8;
            MetaFilePict.xExt = olePress.dwExtentX;
            MetaFilePict.yExt = olePress.dwExtentY;
            MetaFilePict.hMF = 0;

            /* Get Metafile Data */
            pOleStreamData[1].pData = HeapAlloc(GetProcessHeap(),0,pOleStreamData[1].dwDataLength);
            memcpy(pOleStreamData[1].pData, &MetaFilePict, sizeof(MetaFilePict));
            IStream_Read(pStream, &(pOleStreamData[1].pData[sizeof(MetaFilePict)]), pOleStreamData[1].dwDataLength-sizeof(METAFILEPICT16), NULL);
        }
        IStream_Release(pStream);
    }
}

/*************************************************************************
 * OleConvertOLESTREAMToIStorage [OLE32.@]
 *
 * Read info on MSDN
 *
 * TODO
 *      DVTARGETDEVICE parameter is not handled
 *      Still unsure of some mem fields for OLE 10 Stream
 *      Still some unknowns for the IStorage: "\002OlePres000", "\001CompObj",
 *      and "\001OLE" streams
 *
 */
HRESULT WINAPI OleConvertOLESTREAMToIStorage (
    LPOLESTREAM pOleStream,
    LPSTORAGE pstg,
    const DVTARGETDEVICE* ptd)
{
    int i;
    HRESULT hRes=S_OK;
    OLECONVERT_OLESTREAM_DATA pOleStreamData[2];

    TRACE("%p %p %p\n", pOleStream, pstg, ptd);

    memset(pOleStreamData, 0, sizeof(pOleStreamData));

    if(ptd != NULL)
    {
        FIXME("DVTARGETDEVICE is not NULL, unhandled parameter\n");
    }

    if(pstg == NULL || pOleStream == NULL)
    {
        hRes = E_INVALIDARG;
    }

    if(hRes == S_OK)
    {
        /* Load the OLESTREAM to Memory */
        hRes = OLECONVERT_LoadOLE10(pOleStream, &pOleStreamData[0], TRUE);
    }

    if(hRes == S_OK)
    {
        /* Load the OLESTREAM to Memory (part 2)*/
        hRes = OLECONVERT_LoadOLE10(pOleStream, &pOleStreamData[1], FALSE);
    }

    if(hRes == S_OK)
    {

        if(pOleStreamData[0].dwDataLength > sizeof(STORAGE_magic))
        {
            /* Do we have the IStorage Data in the OLESTREAM */
            if(memcmp(pOleStreamData[0].pData, STORAGE_magic, sizeof(STORAGE_magic)) ==0)
            {
                OLECONVERT_GetOLE20FromOLE10(pstg, pOleStreamData[0].pData, pOleStreamData[0].dwDataLength);
                OLECONVERT_CreateOlePresStream(pstg, pOleStreamData[1].dwMetaFileWidth, pOleStreamData[1].dwMetaFileHeight, pOleStreamData[1].pData, pOleStreamData[1].dwDataLength);
            }
            else
            {
                /* It must be an original OLE 1.0 source */
                OLECONVERT_CreateOle10NativeStream(pstg, pOleStreamData[0].pData, pOleStreamData[0].dwDataLength);
            }
        }
        else
        {
            /* It must be an original OLE 1.0 source */
            OLECONVERT_CreateOle10NativeStream(pstg, pOleStreamData[0].pData, pOleStreamData[0].dwDataLength);
        }

        /* Create CompObj Stream if necessary */
        hRes = OLECONVERT_CreateCompObjStream(pstg, pOleStreamData[0].strOleTypeName);
        if(hRes == S_OK)
        {
            /*Create the Ole Stream if necessary */
            STORAGE_CreateOleStream(pstg, 0);
        }
    }


    /* Free allocated memory */
    for(i=0; i < 2; i++)
    {
        HeapFree(GetProcessHeap(),0,pOleStreamData[i].pData);
        HeapFree(GetProcessHeap(),0,pOleStreamData[i].pstrOleObjFileName);
        pOleStreamData[i].pstrOleObjFileName = NULL;
    }
    return hRes;
}

/*************************************************************************
 * OleConvertIStorageToOLESTREAM [OLE32.@]
 *
 * Read info on MSDN
 *
 * Read info on MSDN
 *
 * TODO
 *      Still unsure of some mem fields for OLE 10 Stream
 *      Still some unknowns for the IStorage: "\002OlePres000", "\001CompObj",
 *      and "\001OLE" streams.
 *
 */
HRESULT WINAPI OleConvertIStorageToOLESTREAM (
    LPSTORAGE pstg,
    LPOLESTREAM pOleStream)
{
    int i;
    HRESULT hRes = S_OK;
    IStream *pStream;
    OLECONVERT_OLESTREAM_DATA pOleStreamData[2];

    TRACE("%p %p\n", pstg, pOleStream);

    memset(pOleStreamData, 0, sizeof(pOleStreamData));

    if(pstg == NULL || pOleStream == NULL)
    {
        hRes = E_INVALIDARG;
    }
    if(hRes == S_OK)
    {
        /* Get the ProgID */
        pOleStreamData[0].dwOleTypeNameLength = OLESTREAM_MAX_STR_LEN;
        hRes = OLECONVERT_GetOLE10ProgID(pstg, pOleStreamData[0].strOleTypeName, &(pOleStreamData[0].dwOleTypeNameLength));
    }
    if(hRes == S_OK)
    {
        /* Was it originally Ole10 */
        hRes = IStorage_OpenStream(pstg, L"\1Ole10Native", 0, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &pStream);
        if(hRes == S_OK)
        {
            IStream_Release(pStream);
            /* Get Presentation Data for Ole10Native */
            OLECONVERT_GetOle10PresData(pstg, pOleStreamData);
        }
        else
        {
            /* Get Presentation Data (OLE20) */
            OLECONVERT_GetOle20PresData(pstg, pOleStreamData);
        }

        /* Save OLESTREAM */
        hRes = OLECONVERT_SaveOLE10(&(pOleStreamData[0]), pOleStream);
        if(hRes == S_OK)
        {
            hRes = OLECONVERT_SaveOLE10(&(pOleStreamData[1]), pOleStream);
        }

    }

    /* Free allocated memory */
    for(i=0; i < 2; i++)
    {
        HeapFree(GetProcessHeap(),0,pOleStreamData[i].pData);
    }

    return hRes;
}

enum stream_1ole_flags {
    OleStream_LinkedObject = 0x00000001,
    OleStream_Convert      = 0x00000004
};

/*************************************************************************
 * OleConvertIStorageToOLESTREAMEx [OLE32.@]
 */
HRESULT WINAPI OleConvertIStorageToOLESTREAMEx ( LPSTORAGE stg, CLIPFORMAT cf, LONG width, LONG height,
                                                 DWORD size, LPSTGMEDIUM medium, LPOLESTREAM olestream )
{
    FIXME("%p, %x, %ld, %ld, %ld, %p, %p: stub\n", stg, cf, width, height, size, medium, olestream);

    return E_NOTIMPL;
}

/***********************************************************************
 *		SetConvertStg (OLE32.@)
 */
HRESULT WINAPI SetConvertStg(IStorage *storage, BOOL convert)
{
    DWORD flags = convert ? OleStream_Convert : 0;
    IStream *stream;
    DWORD header[2];
    HRESULT hr;

    TRACE("(%p, %d)\n", storage, convert);

    hr = IStorage_OpenStream(storage, L"\1Ole", NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stream);
    if (FAILED(hr))
    {
        if (hr != STG_E_FILENOTFOUND)
            return hr;

        return STORAGE_CreateOleStream(storage, flags);
    }

    hr = IStream_Read(stream, header, sizeof(header), NULL);
    if (FAILED(hr))
    {
        IStream_Release(stream);
        return hr;
    }

    /* update flag if differs */
    if ((header[1] ^ flags) & OleStream_Convert)
    {
        LARGE_INTEGER pos = {{0}};

        if (header[1] & OleStream_Convert)
            flags = header[1] & ~OleStream_Convert;
        else
            flags = header[1] |  OleStream_Convert;

        pos.QuadPart = sizeof(DWORD);
        hr = IStream_Seek(stream, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            IStream_Release(stream);
            return hr;
        }

        hr = IStream_Write(stream, &flags, sizeof(flags), NULL);
    }

    IStream_Release(stream);
    return hr;
}
