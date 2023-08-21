/*
 * Unit test suite for MLANG APIs.
 *
 * Copyright 2004 Dmitry Timoshkov
 * Copyright 2009 Detlef Riekenberg
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
 */

#define COBJMACROS

#include <stdarg.h>
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "mlang.h"
#include "mimeole.h"

#include "wine/test.h"

static void test_BreakLineA(IMLangLineBreakConsole *mlbc)
{
    LCID locale = 1024;
    UINT uCodePage = CP_USASCII;
    CHAR pszSrc[100];
    LONG cchMax = 20;
    LONG cMaxColumns;
    LONG cchLine, cchSkip;
    HRESULT res;

    // res = StringCchLengthA(pszSrc, cchMax, &cchLength);
    uCodePage = CP_USASCII;
    cMaxColumns = 10;
    pszSrc[100] = "StringWithoutAnySpaces";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 20, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    pszSrc[100] = "  String               With Spaces";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 10, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    pszSrc[100] = "                          First line with spaces";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 10, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    pszSrc[100] = "\tString \t\tWith\tSpaces\tAndTabs";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 10, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    pszSrc[100] = ",String, ,With,Commas and Spaces";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 10, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    pszSrc[100] = " S t r i n g S i n g l e l e t t e r ";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 10, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

    uCodePage = 10; // Some random value
    pszSrc[100] = "StringWithoutAnySpaces";
    res = IMLangLineBreakConsole_BreakLineA(mlbc, locale, uCodePage, pszSrc, cchMax, cMaxColumns, &cchLine, &cchSkip);
    ok(res == S_OK, "got %08lx\n", res);
    ok(cchLine == 20, "got %li\n", cchLine);
    ok(cchSkip == 0, "got %li\n", cchSkip);

}

START_TEST(linebreakconsole)
{
    IMLangLineBreakConsole *pMLangLineBreakConsole = NULL;
    HRESULT res;

    CoInitialize(NULL);

    trace("IMLangLineBreakConsole\n");
    res = CoCreateInstance(&CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IMLangLineBreakConsole, (void **)&pMLangLineBreakConsole);
    if (res == S_OK)
    {
        test_BreakLineA(pMLangLineBreakConsole);
        IMLangLineBreakConsole_Release(pMLangLineBreakConsole);
    }

    CoUninitialize();
}
