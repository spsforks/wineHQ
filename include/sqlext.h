/*
 * MS SQL Extension definitions
 *
 * Copyright (C) 1999 Xiang Li
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __SQLEXT_H
#define __SQLEXT_H

#include <sql.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SQL_SPEC_MAJOR     3
#define SQL_SPEC_MINOR	   51
#define SQL_SPEC_STRING   "03.51"

#define SQL_SQLSTATE_SIZE	5
#define SQL_MAX_DSN_LENGTH	32

#define SQL_MAX_OPTION_STRING_LENGTH    256

#if (ODBCVER < 0x0300)
#define SQL_NO_DATA_FOUND	100
#else
#define SQL_NO_DATA_FOUND	SQL_NO_DATA
#endif

#if (ODBCVER >= 0x0300)
#define	SQL_HANDLE_SENV		5
#endif

#if (ODBCVER >= 0x0300)
#define SQL_ATTR_ODBC_VERSION				200
#define SQL_ATTR_CONNECTION_POOLING			201
#define SQL_ATTR_CP_MATCH					202
#endif

#if (ODBCVER >= 0x0300)
#define SQL_CP_OFF							0UL
#define SQL_CP_ONE_PER_DRIVER				1UL
#define SQL_CP_ONE_PER_HENV					2UL
#define SQL_CP_DEFAULT						SQL_CP_OFF

#define SQL_CP_STRICT_MATCH					0UL
#define SQL_CP_RELAXED_MATCH				1UL
#define SQL_CP_MATCH_DEFAULT				SQL_CP_STRICT_MATCH

#define SQL_OV_ODBC2						2UL
#define	SQL_OV_ODBC3						3UL
#endif

#define SQL_ACCESS_MODE                 101
#define SQL_AUTOCOMMIT                  102
#define SQL_LOGIN_TIMEOUT               103
#define SQL_OPT_TRACE                   104
#define SQL_OPT_TRACEFILE               105
#define SQL_TRANSLATE_DLL               106
#define SQL_TRANSLATE_OPTION            107
#define SQL_TXN_ISOLATION               108
#define SQL_CURRENT_QUALIFIER           109
#define SQL_ODBC_CURSORS                110
#define SQL_QUIET_MODE                  111
#define SQL_PACKET_SIZE                 112

#if (ODBCVER >= 0x0300)
#define SQL_ATTR_ACCESS_MODE		SQL_ACCESS_MODE
#define SQL_ATTR_AUTOCOMMIT			SQL_AUTOCOMMIT
#define SQL_ATTR_CONNECTION_TIMEOUT	113
#define SQL_ATTR_CURRENT_CATALOG	SQL_CURRENT_QUALIFIER
#define SQL_ATTR_DISCONNECT_BEHAVIOR	114
#define SQL_ATTR_ENLIST_IN_DTC		1207
#define SQL_ATTR_ENLIST_IN_XA		1208
#define SQL_ATTR_LOGIN_TIMEOUT		SQL_LOGIN_TIMEOUT
#define SQL_ATTR_ODBC_CURSORS		SQL_ODBC_CURSORS
#define SQL_ATTR_PACKET_SIZE		SQL_PACKET_SIZE
#define SQL_ATTR_QUIET_MODE			SQL_QUIET_MODE
#define SQL_ATTR_TRACE				SQL_OPT_TRACE
#define SQL_ATTR_TRACEFILE			SQL_OPT_TRACEFILE
#define SQL_ATTR_TRANSLATE_LIB		SQL_TRANSLATE_DLL
#define SQL_ATTR_TRANSLATE_OPTION	SQL_TRANSLATE_OPTION
#define SQL_ATTR_TXN_ISOLATION		SQL_TXN_ISOLATION
#endif

#define SQL_ATTR_CONNECTION_DEAD	1209

#if (ODBCVER >= 0x0351)
#define SQL_ATTR_ANSI_APP			115
#endif

#if (ODBCVER < 0x0300)
#define SQL_CONNECT_OPT_DRVR_START      1000
#endif

#if (ODBCVER < 0x0300)
#define SQL_CONN_OPT_MAX                SQL_PACKET_SIZE
#define SQL_CONN_OPT_MIN                SQL_ACCESS_MODE
#endif

#define SQL_MODE_READ_WRITE             0UL
#define SQL_MODE_READ_ONLY              1UL
#define SQL_MODE_DEFAULT                SQL_MODE_READ_WRITE

#define SQL_AUTOCOMMIT_OFF              0UL
#define SQL_AUTOCOMMIT_ON               1UL
#define SQL_AUTOCOMMIT_DEFAULT          SQL_AUTOCOMMIT_ON

#define SQL_LOGIN_TIMEOUT_DEFAULT       15UL

#define SQL_OPT_TRACE_OFF               0UL
#define SQL_OPT_TRACE_ON                1UL
#define SQL_OPT_TRACE_DEFAULT           SQL_OPT_TRACE_OFF
#define SQL_OPT_TRACE_FILE_DEFAULT      "\\SQL.LOG"

#define SQL_CUR_USE_IF_NEEDED           0UL
#define SQL_CUR_USE_ODBC                1UL
#define SQL_CUR_USE_DRIVER              2UL
#define SQL_CUR_DEFAULT                 SQL_CUR_USE_DRIVER

#if (ODBCVER >= 0x0300)
#define SQL_DB_RETURN_TO_POOL			0UL
#define SQL_DB_DISCONNECT				1UL
#define SQL_DB_DEFAULT					SQL_DB_RETURN_TO_POOL

#define SQL_DTC_DONE					0L
#endif

#define SQL_CD_TRUE					1L
#define SQL_CD_FALSE					0L

#if (ODBCVER >= 0x0351)
#define SQL_AA_TRUE					1L
#define SQL_AA_FALSE					0L
#endif

#define SQL_QUERY_TIMEOUT		0
#define SQL_MAX_ROWS			1
#define SQL_NOSCAN			2
#define SQL_MAX_LENGTH			3
#define SQL_ASYNC_ENABLE		4
#define SQL_BIND_TYPE			5
#define SQL_CURSOR_TYPE			6
#define SQL_CONCURRENCY			7
#define SQL_KEYSET_SIZE			8
#define SQL_ROWSET_SIZE			9
#define SQL_SIMULATE_CURSOR		10
#define SQL_RETRIEVE_DATA		11
#define SQL_USE_BOOKMARKS		12
#define SQL_GET_BOOKMARK		13
#define SQL_ROW_NUMBER			14

#if (ODBCVER >= 0x0300)
#define SQL_ATTR_ASYNC_ENABLE				4
#define SQL_ATTR_CONCURRENCY				SQL_CONCURRENCY
#define SQL_ATTR_CURSOR_TYPE				SQL_CURSOR_TYPE
#define	SQL_ATTR_ENABLE_AUTO_IPD			15
#define SQL_ATTR_FETCH_BOOKMARK_PTR			16
#define SQL_ATTR_KEYSET_SIZE				SQL_KEYSET_SIZE
#define SQL_ATTR_MAX_LENGTH				SQL_MAX_LENGTH
#define SQL_ATTR_MAX_ROWS				SQL_MAX_ROWS
#define SQL_ATTR_NOSCAN					SQL_NOSCAN
#define SQL_ATTR_PARAM_BIND_OFFSET_PTR			17
#define	SQL_ATTR_PARAM_BIND_TYPE			18
#define SQL_ATTR_PARAM_OPERATION_PTR			19
#define SQL_ATTR_PARAM_STATUS_PTR			20
#define	SQL_ATTR_PARAMS_PROCESSED_PTR			21
#define SQL_ATTR_RETRIEVE_DATA				SQL_RETRIEVE_DATA
#define SQL_ATTR_ROW_BIND_OFFSET_PTR			23
#define	SQL_ATTR_ROW_BIND_TYPE				SQL_BIND_TYPE
#define SQL_ATTR_ROW_NUMBER				SQL_ROW_NUMBER
#define SQL_ATTR_ROW_OPERATION_PTR			24
#define	SQL_ATTR_ROW_STATUS_PTR				25
#define	SQL_ATTR_ROWS_FETCHED_PTR			26
#define SQL_ATTR_ROW_ARRAY_SIZE				27
#define SQL_ATTR_SIMULATE_CURSOR			SQL_SIMULATE_CURSOR
#define SQL_ATTR_USE_BOOKMARKS				SQL_USE_BOOKMARKS

#endif

#if (ODBCVER < 0x0300)
#define SQL_STMT_OPT_MAX                SQL_ROW_NUMBER
#define SQL_STMT_OPT_MIN	SQL_QUERY_TIMEOUT
#endif

#if (ODBCVER >= 0x0300)
#define	SQL_COL_PRED_CHAR		SQL_LIKE_ONLY
#define	SQL_COL_PRED_BASIC		SQL_ALL_EXCEPT_LIKE
#endif

#if (ODBCVER >= 0x0300)
#define SQL_IS_POINTER							(-4)
#define SQL_IS_UINTEGER							(-5)
#define SQL_IS_INTEGER							(-6)
#define SQL_IS_USMALLINT						(-7)
#define SQL_IS_SMALLINT							(-8)
#endif

#if (ODBCVER >= 0x0300)
#define SQL_PARAM_BIND_BY_COLUMN			0UL
#define SQL_PARAM_BIND_TYPE_DEFAULT			SQL_PARAM_BIND_BY_COLUMN
#endif

#define SQL_QUERY_TIMEOUT_DEFAULT       0UL

#define SQL_MAX_ROWS_DEFAULT            0UL

#define SQL_NOSCAN_OFF                  0UL
#define SQL_NOSCAN_ON                   1UL
#define SQL_NOSCAN_DEFAULT              SQL_NOSCAN_OFF

#define SQL_MAX_LENGTH_DEFAULT          0UL

#define SQL_ASYNC_ENABLE_OFF			0UL
#define SQL_ASYNC_ENABLE_ON				1UL
#define SQL_ASYNC_ENABLE_DEFAULT        SQL_ASYNC_ENABLE_OFF

#define SQL_BIND_BY_COLUMN              0UL
#define SQL_BIND_TYPE_DEFAULT           SQL_BIND_BY_COLUMN

#define SQL_CONCUR_READ_ONLY            1
#define SQL_CONCUR_LOCK                 2
#define SQL_CONCUR_ROWVER               3
#define SQL_CONCUR_VALUES               4
#define SQL_CONCUR_DEFAULT              SQL_CONCUR_READ_ONLY

#define SQL_CURSOR_FORWARD_ONLY         0UL
#define SQL_CURSOR_KEYSET_DRIVEN        1UL
#define SQL_CURSOR_DYNAMIC              2UL
#define SQL_CURSOR_STATIC               3UL
#define SQL_CURSOR_TYPE_DEFAULT         SQL_CURSOR_FORWARD_ONLY

#define SQL_ROWSET_SIZE_DEFAULT         1UL

#define SQL_KEYSET_SIZE_DEFAULT         0UL

#define SQL_SC_NON_UNIQUE               0UL
#define SQL_SC_TRY_UNIQUE               1UL
#define SQL_SC_UNIQUE                   2UL

#define SQL_RD_OFF                      0UL
#define SQL_RD_ON                       1UL
#define SQL_RD_DEFAULT                  SQL_RD_ON

#define SQL_UB_OFF                      0UL
#define	SQL_UB_ON			01UL
#define SQL_UB_DEFAULT                  SQL_UB_OFF

#if (ODBCVER >= 0x0300)
#define SQL_UB_FIXED					SQL_UB_ON
#define SQL_UB_VARIABLE					2UL
#endif

#if (ODBCVER >= 0x0300)
#define SQL_DESC_ARRAY_SIZE					20
#define SQL_DESC_ARRAY_STATUS_PTR				21
#define SQL_DESC_AUTO_UNIQUE_VALUE				SQL_COLUMN_AUTO_INCREMENT
#define SQL_DESC_BASE_COLUMN_NAME				22
#define SQL_DESC_BASE_TABLE_NAME				23
#define SQL_DESC_BIND_OFFSET_PTR				24
#define SQL_DESC_BIND_TYPE					25
#define SQL_DESC_CASE_SENSITIVE					SQL_COLUMN_CASE_SENSITIVE
#define SQL_DESC_CATALOG_NAME					SQL_COLUMN_QUALIFIER_NAME
#define SQL_DESC_CONCISE_TYPE					SQL_COLUMN_TYPE
#define SQL_DESC_DATETIME_INTERVAL_PRECISION			26
#define SQL_DESC_DISPLAY_SIZE					SQL_COLUMN_DISPLAY_SIZE
#define SQL_DESC_FIXED_PREC_SCALE				SQL_COLUMN_MONEY
#define SQL_DESC_LABEL						SQL_COLUMN_LABEL
#define SQL_DESC_LITERAL_PREFIX					27
#define SQL_DESC_LITERAL_SUFFIX					28
#define SQL_DESC_LOCAL_TYPE_NAME				29
#define	SQL_DESC_MAXIMUM_SCALE					30
#define SQL_DESC_MINIMUM_SCALE					31
#define SQL_DESC_NUM_PREC_RADIX					32
#define SQL_DESC_PARAMETER_TYPE					33
#define SQL_DESC_ROWS_PROCESSED_PTR				34
#if (ODBCVER >= 0x0350)
#define SQL_DESC_ROWVER						35
#endif
#define SQL_DESC_SCHEMA_NAME					SQL_COLUMN_OWNER_NAME
#define SQL_DESC_SEARCHABLE					SQL_COLUMN_SEARCHABLE
#define SQL_DESC_TYPE_NAME					SQL_COLUMN_TYPE_NAME
#define SQL_DESC_TABLE_NAME					SQL_COLUMN_TABLE_NAME
#define SQL_DESC_UNSIGNED					SQL_COLUMN_UNSIGNED
#define SQL_DESC_UPDATABLE					SQL_COLUMN_UPDATABLE
#endif

#if (ODBCVER >= 0x0300)
#define SQL_DIAG_CURSOR_ROW_COUNT			(-1249)
#define SQL_DIAG_ROW_NUMBER				(-1248)
#define SQL_DIAG_COLUMN_NUMBER				(-1247)
#endif

#define SQL_DATE                                9
#if (ODBCVER >= 0x0300)
#define SQL_INTERVAL				10
#endif
#define SQL_TIME                                10
#define SQL_TIMESTAMP                           11
#define SQL_LONGVARCHAR                         (-1)
#define SQL_BINARY                              (-2)
#define SQL_VARBINARY                           (-3)
#define SQL_LONGVARBINARY                       (-4)
#define SQL_BIGINT                              (-5)
#define SQL_TINYINT                             (-6)
#define SQL_BIT                                 (-7)
#if (ODBCVER >= 0x0350)
#define SQL_GUID				(-11)
#endif

#if (ODBCVER >= 0x0300)
#define SQL_CODE_YEAR				1
#define SQL_CODE_MONTH				2
#define SQL_CODE_DAY				3
#define SQL_CODE_HOUR				4
#define SQL_CODE_MINUTE				5
#define SQL_CODE_SECOND				6
#define SQL_CODE_YEAR_TO_MONTH			7
#define SQL_CODE_DAY_TO_HOUR			8
#define SQL_CODE_DAY_TO_MINUTE			9
#define SQL_CODE_DAY_TO_SECOND			10
#define SQL_CODE_HOUR_TO_MINUTE			11
#define SQL_CODE_HOUR_TO_SECOND			12
#define SQL_CODE_MINUTE_TO_SECOND		13

#define SQL_INTERVAL_YEAR			(100 + SQL_CODE_YEAR)
#define SQL_INTERVAL_MONTH			(100 + SQL_CODE_MONTH)
#define SQL_INTERVAL_DAY			(100 + SQL_CODE_DAY)
#define SQL_INTERVAL_HOUR			(100 + SQL_CODE_HOUR)
#define SQL_INTERVAL_MINUTE			(100 + SQL_CODE_MINUTE)
#define SQL_INTERVAL_SECOND                	(100 + SQL_CODE_SECOND)
#define SQL_INTERVAL_YEAR_TO_MONTH		(100 + SQL_CODE_YEAR_TO_MONTH)
#define SQL_INTERVAL_DAY_TO_HOUR		(100 + SQL_CODE_DAY_TO_HOUR)
#define SQL_INTERVAL_DAY_TO_MINUTE		(100 + SQL_CODE_DAY_TO_MINUTE)
#define SQL_INTERVAL_DAY_TO_SECOND		(100 + SQL_CODE_DAY_TO_SECOND)
#define SQL_INTERVAL_HOUR_TO_MINUTE		(100 + SQL_CODE_HOUR_TO_MINUTE)
#define SQL_INTERVAL_HOUR_TO_SECOND		(100 + SQL_CODE_HOUR_TO_SECOND)
#define SQL_INTERVAL_MINUTE_TO_SECOND		(100 + SQL_CODE_MINUTE_TO_SECOND)

#else
#define SQL_INTERVAL_YEAR                       (-80)
#define SQL_INTERVAL_MONTH                      (-81)
#define SQL_INTERVAL_YEAR_TO_MONTH              (-82)
#define SQL_INTERVAL_DAY                        (-83)
#define SQL_INTERVAL_HOUR                       (-84)
#define SQL_INTERVAL_MINUTE                     (-85)
#define SQL_INTERVAL_SECOND                     (-86)
#define SQL_INTERVAL_DAY_TO_HOUR                (-87)
#define SQL_INTERVAL_DAY_TO_MINUTE              (-88)
#define SQL_INTERVAL_DAY_TO_SECOND              (-89)
#define SQL_INTERVAL_HOUR_TO_MINUTE             (-90)
#define SQL_INTERVAL_HOUR_TO_SECOND             (-91)
#define SQL_INTERVAL_MINUTE_TO_SECOND           (-92)
#endif


#if (ODBCVER <= 0x0300)
#define SQL_UNICODE                             (-95)
#define SQL_UNICODE_VARCHAR                     (-96)
#define SQL_UNICODE_LONGVARCHAR                 (-97)
#define SQL_UNICODE_CHAR                        SQL_UNICODE
#else

#define	SQL_UNICODE			SQL_WCHAR
#define	SQL_UNICODE_VARCHAR		SQL_WVARCHAR
#define SQL_UNICODE_LONGVARCHAR	SQL_WLONGVARCHAR
#define SQL_UNICODE_CHAR		SQL_WCHAR
#endif

#if (ODBCVER < 0x0300)
#define SQL_TYPE_DRIVER_START                   SQL_INTERVAL_YEAR
#define SQL_TYPE_DRIVER_END                     SQL_UNICODE_LONGVARCHAR
#endif

#define SQL_C_CHAR    SQL_CHAR
#define SQL_C_LONG    SQL_INTEGER
#define SQL_C_SHORT   SQL_SMALLINT
#define SQL_C_FLOAT   SQL_REAL
#define SQL_C_DOUBLE  SQL_DOUBLE
#if (ODBCVER >= 0x0300)
#define	SQL_C_NUMERIC		SQL_NUMERIC
#endif
#define SQL_C_DEFAULT 99

#define SQL_SIGNED_OFFSET       (-20)
#define SQL_UNSIGNED_OFFSET     (-22)

#define SQL_C_DATE       SQL_DATE
#define SQL_C_TIME       SQL_TIME
#define SQL_C_TIMESTAMP  SQL_TIMESTAMP
#if (ODBCVER >= 0x0300)
#define SQL_C_TYPE_DATE				SQL_TYPE_DATE
#define SQL_C_TYPE_TIME				SQL_TYPE_TIME
#define SQL_C_TYPE_TIMESTAMP			SQL_TYPE_TIMESTAMP
#define SQL_C_INTERVAL_YEAR			SQL_INTERVAL_YEAR
#define SQL_C_INTERVAL_MONTH			SQL_INTERVAL_MONTH
#define SQL_C_INTERVAL_DAY			SQL_INTERVAL_DAY
#define SQL_C_INTERVAL_HOUR			SQL_INTERVAL_HOUR
#define SQL_C_INTERVAL_MINUTE			SQL_INTERVAL_MINUTE
#define SQL_C_INTERVAL_SECOND			SQL_INTERVAL_SECOND
#define SQL_C_INTERVAL_YEAR_TO_MONTH		SQL_INTERVAL_YEAR_TO_MONTH
#define SQL_C_INTERVAL_DAY_TO_HOUR		SQL_INTERVAL_DAY_TO_HOUR
#define SQL_C_INTERVAL_DAY_TO_MINUTE		SQL_INTERVAL_DAY_TO_MINUTE
#define SQL_C_INTERVAL_DAY_TO_SECOND		SQL_INTERVAL_DAY_TO_SECOND
#define SQL_C_INTERVAL_HOUR_TO_MINUTE		SQL_INTERVAL_HOUR_TO_MINUTE
#define SQL_C_INTERVAL_HOUR_TO_SECOND		SQL_INTERVAL_HOUR_TO_SECOND
#define SQL_C_INTERVAL_MINUTE_TO_SECOND		SQL_INTERVAL_MINUTE_TO_SECOND
#endif
#define SQL_C_BINARY     SQL_BINARY
#define SQL_C_BIT        SQL_BIT
#if (ODBCVER >= 0x0300)
#define SQL_C_SBIGINT	(SQL_BIGINT+SQL_SIGNED_OFFSET)
#define SQL_C_UBIGINT	(SQL_BIGINT+SQL_UNSIGNED_OFFSET)
#endif
#define SQL_C_TINYINT    SQL_TINYINT
#define SQL_C_SLONG      (SQL_C_LONG+SQL_SIGNED_OFFSET)
#define SQL_C_SSHORT     (SQL_C_SHORT+SQL_SIGNED_OFFSET)
#define SQL_C_STINYINT   (SQL_TINYINT+SQL_SIGNED_OFFSET)
#define SQL_C_ULONG      (SQL_C_LONG+SQL_UNSIGNED_OFFSET)
#define SQL_C_USHORT     (SQL_C_SHORT+SQL_UNSIGNED_OFFSET)
#define SQL_C_UTINYINT   (SQL_TINYINT+SQL_UNSIGNED_OFFSET)
#define SQL_C_BOOKMARK   SQL_C_ULONG

#if (ODBCVER >= 0x0350)
#define SQL_C_GUID	SQL_GUID
#endif

#define SQL_TYPE_NULL                   0
#if (ODBCVER < 0x0300)
#define SQL_TYPE_MIN                    SQL_BIT
#define SQL_TYPE_MAX                    SQL_VARCHAR
#endif

#if (ODBCVER >= 0x0300)
#define SQL_C_VARBOOKMARK		SQL_C_BINARY
#endif

#if (ODBCVER >= 0x0300)
#define SQL_NO_ROW_NUMBER					(-1)
#define SQL_NO_COLUMN_NUMBER					(-1)
#define SQL_ROW_NUMBER_UNKNOWN					(-2)
#define SQL_COLUMN_NUMBER_UNKNOWN				(-2)
#endif

#define SQL_DEFAULT_PARAM            (-5)
#define SQL_IGNORE                   (-6)
#if (ODBCVER >= 0x0300)
#define SQL_COLUMN_IGNORE		SQL_IGNORE
#endif
#define SQL_LEN_DATA_AT_EXEC_OFFSET  (-100)
#define SQL_LEN_DATA_AT_EXEC(length) (-(length)+SQL_LEN_DATA_AT_EXEC_OFFSET)

#define SQL_LEN_BINARY_ATTR_OFFSET	 (-100)
#define SQL_LEN_BINARY_ATTR(length)	 (-(length)+SQL_LEN_BINARY_ATTR_OFFSET)

#define SQL_PARAM_TYPE_DEFAULT           SQL_PARAM_INPUT_OUTPUT
#define SQL_SETPARAM_VALUE_MAX           (-1L)

#define SQL_COLUMN_COUNT                0
#define SQL_COLUMN_NAME                 1
#define SQL_COLUMN_TYPE                 2
#define SQL_COLUMN_LENGTH               3
#define SQL_COLUMN_PRECISION            4
#define SQL_COLUMN_SCALE                5
#define SQL_COLUMN_DISPLAY_SIZE         6
#define SQL_COLUMN_NULLABLE             7
#define SQL_COLUMN_UNSIGNED             8
#define SQL_COLUMN_MONEY                9
#define SQL_COLUMN_UPDATABLE            10
#define SQL_COLUMN_AUTO_INCREMENT       11
#define SQL_COLUMN_CASE_SENSITIVE       12
#define SQL_COLUMN_SEARCHABLE           13
#define SQL_COLUMN_TYPE_NAME            14
#define SQL_COLUMN_TABLE_NAME           15
#define SQL_COLUMN_OWNER_NAME           16
#define SQL_COLUMN_QUALIFIER_NAME       17
#define SQL_COLUMN_LABEL                18
#define SQL_COLATT_OPT_MAX              SQL_COLUMN_LABEL
#if (ODBCVER < 0x0300)
#define SQL_COLUMN_DRIVER_START         1000
#endif

#define SQL_COLATT_OPT_MIN              SQL_COLUMN_COUNT

#define SQL_ATTR_READONLY               0
#define SQL_ATTR_WRITE                  1
#define SQL_ATTR_READWRITE_UNKNOWN      2

#define SQL_UNSEARCHABLE                0
#define SQL_LIKE_ONLY                   1
#define SQL_ALL_EXCEPT_LIKE             2
#define SQL_SEARCHABLE                  3
#define SQL_PRED_SEARCHABLE				SQL_SEARCHABLE


#define SQL_NO_TOTAL                    (-4)

#if (ODBCVER >= 0x0300)
#define SQL_API_SQLALLOCHANDLESTD	73
#define SQL_API_SQLBULKOPERATIONS	24
#endif
#define SQL_API_SQLBINDPARAMETER    72
#define SQL_API_SQLBROWSECONNECT    55
#define SQL_API_SQLCOLATTRIBUTES    6
#define SQL_API_SQLCOLUMNPRIVILEGES 56
#define SQL_API_SQLDESCRIBEPARAM    58
#define	SQL_API_SQLDRIVERCONNECT	41
#define SQL_API_SQLDRIVERS          71
#define SQL_API_SQLEXTENDEDFETCH    59
#define SQL_API_SQLFOREIGNKEYS      60
#define SQL_API_SQLMORERESULTS      61
#define SQL_API_SQLNATIVESQL        62
#define SQL_API_SQLNUMPARAMS        63
#define SQL_API_SQLPARAMOPTIONS     64
#define SQL_API_SQLPRIMARYKEYS      65
#define SQL_API_SQLPROCEDURECOLUMNS 66
#define SQL_API_SQLPROCEDURES       67
#define SQL_API_SQLSETPOS           68
#define SQL_API_SQLSETSCROLLOPTIONS 69
#define SQL_API_SQLTABLEPRIVILEGES  70

#if (ODBCVER < 0x0300)
#define SQL_EXT_API_LAST            SQL_API_SQLBINDPARAMETER
#define SQL_NUM_FUNCTIONS           23
#define SQL_EXT_API_START           40
#define SQL_NUM_EXTENSIONS (SQL_EXT_API_LAST-SQL_EXT_API_START+1)
#endif

#define SQL_API_ALL_FUNCTIONS       0

#define SQL_API_LOADBYORDINAL       199

#if (ODBCVER >= 0x0300)
#define SQL_API_ODBC3_ALL_FUNCTIONS		999
#define	SQL_API_ODBC3_ALL_FUNCTIONS_SIZE	250


#define SQL_FUNC_EXISTS(pfExists, uwAPI) ((*(((UWORD*) (pfExists)) + ((uwAPI) >> 4)) & (1 << ((uwAPI) & 0x000F)) ) ? SQL_TRUE : SQL_FALSE )

#endif

#define SQL_INFO_FIRST                       0
#define SQL_ACTIVE_CONNECTIONS               0
#define SQL_ACTIVE_STATEMENTS                1
#define SQL_DRIVER_HDBC                      3
#define SQL_DRIVER_HENV                      4
#define SQL_DRIVER_HSTMT                     5
#define SQL_DRIVER_NAME                      6
#define SQL_DRIVER_VER                       7
#define SQL_ODBC_API_CONFORMANCE             9
#define SQL_ODBC_VER                        10
#define SQL_ROW_UPDATES                     11
#define SQL_ODBC_SAG_CLI_CONFORMANCE        12
#define SQL_ODBC_SQL_CONFORMANCE            15
#define SQL_PROCEDURES                      21
#define SQL_CONCAT_NULL_BEHAVIOR            22
#define SQL_CURSOR_ROLLBACK_BEHAVIOR        24
#define SQL_EXPRESSIONS_IN_ORDERBY          27
#define SQL_MAX_OWNER_NAME_LEN              32
#define SQL_MAX_PROCEDURE_NAME_LEN          33
#define SQL_MAX_QUALIFIER_NAME_LEN          34
#define SQL_MULT_RESULT_SETS                36
#define SQL_MULTIPLE_ACTIVE_TXN             37
#define SQL_OUTER_JOINS                     38
#define SQL_OWNER_TERM                      39
#define SQL_PROCEDURE_TERM                  40
#define SQL_QUALIFIER_NAME_SEPARATOR        41
#define SQL_QUALIFIER_TERM                  42
#define SQL_SCROLL_OPTIONS                  44
#define SQL_TABLE_TERM                      45
#define SQL_CONVERT_FUNCTIONS               48
#define SQL_NUMERIC_FUNCTIONS               49
#define SQL_STRING_FUNCTIONS                50
#define SQL_SYSTEM_FUNCTIONS                51
#define SQL_TIMEDATE_FUNCTIONS              52
#define SQL_CONVERT_BIGINT                  53
#define SQL_CONVERT_BINARY                  54
#define SQL_CONVERT_BIT                     55
#define SQL_CONVERT_CHAR                    56
#define SQL_CONVERT_DATE                    57
#define SQL_CONVERT_DECIMAL                 58
#define SQL_CONVERT_DOUBLE                  59
#define SQL_CONVERT_FLOAT                   60
#define SQL_CONVERT_INTEGER                 61
#define SQL_CONVERT_LONGVARCHAR             62
#define SQL_CONVERT_NUMERIC                 63
#define SQL_CONVERT_REAL                    64
#define SQL_CONVERT_SMALLINT                65
#define SQL_CONVERT_TIME                    66
#define SQL_CONVERT_TIMESTAMP               67
#define SQL_CONVERT_TINYINT                 68
#define SQL_CONVERT_VARBINARY               69
#define SQL_CONVERT_VARCHAR                 70
#define SQL_CONVERT_LONGVARBINARY           71
#define SQL_ODBC_SQL_OPT_IEF                73
#define SQL_CORRELATION_NAME                74
#define SQL_NON_NULLABLE_COLUMNS            75
#define SQL_DRIVER_HLIB                     76
#define SQL_DRIVER_ODBC_VER                 77
#define SQL_LOCK_TYPES                      78
#define SQL_POS_OPERATIONS                  79
#define SQL_POSITIONED_STATEMENTS           80
#define SQL_BOOKMARK_PERSISTENCE            82
#define SQL_STATIC_SENSITIVITY              83
#define SQL_FILE_USAGE                      84
#define SQL_COLUMN_ALIAS                    87
#define SQL_GROUP_BY                        88
#define SQL_KEYWORDS                        89
#define SQL_OWNER_USAGE                     91
#define SQL_QUALIFIER_USAGE                 92
#define SQL_QUOTED_IDENTIFIER_CASE          93
#define SQL_SUBQUERIES                      95
#define SQL_UNION                           96
#define SQL_MAX_ROW_SIZE_INCLUDES_LONG      103
#define SQL_MAX_CHAR_LITERAL_LEN            108
#define SQL_TIMEDATE_ADD_INTERVALS          109
#define SQL_TIMEDATE_DIFF_INTERVALS         110
#define SQL_NEED_LONG_DATA_LEN              111
#define SQL_MAX_BINARY_LITERAL_LEN          112
#define SQL_LIKE_ESCAPE_CLAUSE              113
#define SQL_QUALIFIER_LOCATION              114

#if (ODBCVER >= 0x0201 && ODBCVER < 0x0300)
#define SQL_OJ_CAPABILITIES         65003
#endif

#if (ODBCVER < 0x0300)
#define SQL_INFO_LAST							SQL_QUALIFIER_LOCATION
#define SQL_INFO_DRIVER_START						1000
#endif

#if (ODBCVER >= 0x0300)
#define SQL_ACTIVE_ENVIRONMENTS						116
#define	SQL_ALTER_DOMAIN						117

#define	SQL_SQL_CONFORMANCE						118
#define SQL_DATETIME_LITERALS						119

#define	SQL_ASYNC_MODE							10021
#define SQL_BATCH_ROW_COUNT						120
#define SQL_BATCH_SUPPORT						121
#define SQL_CATALOG_LOCATION						SQL_QUALIFIER_LOCATION
#define SQL_CATALOG_NAME_SEPARATOR					SQL_QUALIFIER_NAME_SEPARATOR
#define SQL_CATALOG_TERM						SQL_QUALIFIER_TERM
#define SQL_CATALOG_USAGE						SQL_QUALIFIER_USAGE
#define	SQL_CONVERT_WCHAR						122
#define SQL_CONVERT_INTERVAL_DAY_TIME					123
#define SQL_CONVERT_INTERVAL_YEAR_MONTH					124
#define	SQL_CONVERT_WLONGVARCHAR					125
#define	SQL_CONVERT_WVARCHAR						126
#define	SQL_CREATE_ASSERTION						127
#define	SQL_CREATE_CHARACTER_SET					128
#define	SQL_CREATE_COLLATION						129
#define	SQL_CREATE_DOMAIN						130
#define	SQL_CREATE_SCHEMA						131
#define	SQL_CREATE_TABLE						132
#define	SQL_CREATE_TRANSLATION						133
#define	SQL_CREATE_VIEW							134
#define SQL_DRIVER_HDESC						135
#define	SQL_DROP_ASSERTION						136
#define	SQL_DROP_CHARACTER_SET						137
#define	SQL_DROP_COLLATION						138
#define	SQL_DROP_DOMAIN							139
#define	SQL_DROP_SCHEMA							140
#define	SQL_DROP_TABLE							141
#define	SQL_DROP_TRANSLATION						142
#define	SQL_DROP_VIEW							143
#define SQL_DYNAMIC_CURSOR_ATTRIBUTES1			144
#define SQL_DYNAMIC_CURSOR_ATTRIBUTES2			145
#define SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1		146
#define SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2		147
#define SQL_INDEX_KEYWORDS				148
#define SQL_INFO_SCHEMA_VIEWS				149
#define SQL_KEYSET_CURSOR_ATTRIBUTES1			150
#define SQL_KEYSET_CURSOR_ATTRIBUTES2			151
#define	SQL_MAX_ASYNC_CONCURRENT_STATEMENTS		10022
#define SQL_ODBC_INTERFACE_CONFORMANCE			152
#define SQL_PARAM_ARRAY_ROW_COUNTS     			153
#define SQL_PARAM_ARRAY_SELECTS     			154
#define SQL_SCHEMA_TERM					SQL_OWNER_TERM
#define SQL_SCHEMA_USAGE				SQL_OWNER_USAGE
#define SQL_SQL92_DATETIME_FUNCTIONS			155
#define SQL_SQL92_FOREIGN_KEY_DELETE_RULE		156
#define SQL_SQL92_FOREIGN_KEY_UPDATE_RULE		157
#define SQL_SQL92_GRANT					158
#define SQL_SQL92_NUMERIC_VALUE_FUNCTIONS		159
#define SQL_SQL92_PREDICATES				160
#define SQL_SQL92_RELATIONAL_JOIN_OPERATORS		161
#define SQL_SQL92_REVOKE				162
#define SQL_SQL92_ROW_VALUE_CONSTRUCTOR			163
#define SQL_SQL92_STRING_FUNCTIONS			164
#define SQL_SQL92_VALUE_EXPRESSIONS			165
#define SQL_STANDARD_CLI_CONFORMANCE			166
#define SQL_STATIC_CURSOR_ATTRIBUTES1			167
#define SQL_STATIC_CURSOR_ATTRIBUTES2			168

#define SQL_AGGREGATE_FUNCTIONS				169
#define SQL_DDL_INDEX					170
#define SQL_DM_VER					171
#define SQL_INSERT_STATEMENT				172
#define SQL_UNION_STATEMENT				SQL_UNION
#endif

#define	SQL_DTC_TRANSITION_COST				1750

#if (ODBCVER >= 0x0300)

#define	SQL_AT_ADD_COLUMN_SINGLE				0x00000020L
#define	SQL_AT_ADD_COLUMN_DEFAULT				0x00000040L
#define	SQL_AT_ADD_COLUMN_COLLATION				0x00000080L
#define	SQL_AT_SET_COLUMN_DEFAULT				0x00000100L
#define	SQL_AT_DROP_COLUMN_DEFAULT				0x00000200L
#define	SQL_AT_DROP_COLUMN_CASCADE				0x00000400L
#define	SQL_AT_DROP_COLUMN_RESTRICT				0x00000800L
#define SQL_AT_ADD_TABLE_CONSTRAINT				0x00001000L
#define SQL_AT_DROP_TABLE_CONSTRAINT_CASCADE			0x00002000L
#define SQL_AT_DROP_TABLE_CONSTRAINT_RESTRICT			0x00004000L
#define SQL_AT_CONSTRAINT_NAME_DEFINITION			0x00008000L
#define SQL_AT_CONSTRAINT_INITIALLY_DEFERRED			0x00010000L
#define SQL_AT_CONSTRAINT_INITIALLY_IMMEDIATE			0x00020000L
#define SQL_AT_CONSTRAINT_DEFERRABLE				0x00040000L
#define SQL_AT_CONSTRAINT_NON_DEFERRABLE			0x00080000L
#endif

#define SQL_CVT_CHAR                        0x00000001L
#define SQL_CVT_NUMERIC                     0x00000002L
#define SQL_CVT_DECIMAL                     0x00000004L
#define SQL_CVT_INTEGER                     0x00000008L
#define SQL_CVT_SMALLINT                    0x00000010L
#define SQL_CVT_FLOAT                       0x00000020L
#define SQL_CVT_REAL                        0x00000040L
#define SQL_CVT_DOUBLE                      0x00000080L
#define SQL_CVT_VARCHAR                     0x00000100L
#define SQL_CVT_LONGVARCHAR                 0x00000200L
#define SQL_CVT_BINARY                      0x00000400L
#define SQL_CVT_VARBINARY                   0x00000800L
#define SQL_CVT_BIT                         0x00001000L
#define SQL_CVT_TINYINT                     0x00002000L
#define SQL_CVT_BIGINT                      0x00004000L
#define SQL_CVT_DATE                        0x00008000L
#define SQL_CVT_TIME                        0x00010000L
#define SQL_CVT_TIMESTAMP                   0x00020000L
#define SQL_CVT_LONGVARBINARY               0x00040000L
#if (ODBCVER >= 0x0300)
#define SQL_CVT_INTERVAL_YEAR_MONTH	0x00080000L
#define SQL_CVT_INTERVAL_DAY_TIME	0x00100000L
#define	SQL_CVT_WCHAR			0x00200000L
#define	SQL_CVT_WLONGVARCHAR		0x00400000L
#define	SQL_CVT_WVARCHAR		0x00800000L

#endif


#define SQL_FN_CVT_CONVERT		0x00000001L
#if (ODBCVER >= 0x0300)
#define SQL_FN_CVT_CAST			0x00000002L
#endif

#define SQL_FN_STR_CONCAT                   0x00000001L
#define SQL_FN_STR_INSERT                   0x00000002L
#define SQL_FN_STR_LEFT                     0x00000004L
#define SQL_FN_STR_LTRIM                    0x00000008L
#define SQL_FN_STR_LENGTH                   0x00000010L
#define SQL_FN_STR_LOCATE                   0x00000020L
#define SQL_FN_STR_LCASE                    0x00000040L
#define SQL_FN_STR_REPEAT                   0x00000080L
#define SQL_FN_STR_REPLACE                  0x00000100L
#define SQL_FN_STR_RIGHT                    0x00000200L
#define SQL_FN_STR_RTRIM                    0x00000400L
#define SQL_FN_STR_SUBSTRING                0x00000800L
#define SQL_FN_STR_UCASE                    0x00001000L
#define SQL_FN_STR_ASCII                    0x00002000L
#define SQL_FN_STR_CHAR                     0x00004000L
#define SQL_FN_STR_DIFFERENCE               0x00008000L
#define SQL_FN_STR_LOCATE_2                 0x00010000L
#define SQL_FN_STR_SOUNDEX                  0x00020000L
#define SQL_FN_STR_SPACE                    0x00040000L
#if (ODBCVER >= 0x0300)
#define SQL_FN_STR_BIT_LENGTH			0x00080000L
#define SQL_FN_STR_CHAR_LENGTH			0x00100000L
#define SQL_FN_STR_CHARACTER_LENGTH		0x00200000L
#define SQL_FN_STR_OCTET_LENGTH			0x00400000L
#define SQL_FN_STR_POSITION			0x00800000L
#endif

#if (ODBCVER >= 0x0300)
#define SQL_SSF_CONVERT					0x00000001L
#define SQL_SSF_LOWER					0x00000002L
#define SQL_SSF_UPPER					0x00000004L
#define SQL_SSF_SUBSTRING				0x00000008L
#define SQL_SSF_TRANSLATE				0x00000010L
#define SQL_SSF_TRIM_BOTH				0x00000020L
#define SQL_SSF_TRIM_LEADING				0x00000040L
#define SQL_SSF_TRIM_TRAILING				0x00000080L
#endif

#define SQL_FN_NUM_ABS                      0x00000001L
#define SQL_FN_NUM_ACOS                     0x00000002L
#define SQL_FN_NUM_ASIN                     0x00000004L
#define SQL_FN_NUM_ATAN                     0x00000008L
#define SQL_FN_NUM_ATAN2                    0x00000010L
#define SQL_FN_NUM_CEILING                  0x00000020L
#define SQL_FN_NUM_COS                      0x00000040L
#define SQL_FN_NUM_COT                      0x00000080L
#define SQL_FN_NUM_EXP                      0x00000100L
#define SQL_FN_NUM_FLOOR                    0x00000200L
#define SQL_FN_NUM_LOG                      0x00000400L
#define SQL_FN_NUM_MOD                      0x00000800L
#define SQL_FN_NUM_SIGN                     0x00001000L
#define SQL_FN_NUM_SIN                      0x00002000L
#define SQL_FN_NUM_SQRT                     0x00004000L
#define SQL_FN_NUM_TAN                      0x00008000L
#define SQL_FN_NUM_PI                       0x00010000L
#define SQL_FN_NUM_RAND                     0x00020000L
#define SQL_FN_NUM_DEGREES                  0x00040000L
#define SQL_FN_NUM_LOG10                    0x00080000L
#define SQL_FN_NUM_POWER                    0x00100000L
#define SQL_FN_NUM_RADIANS                  0x00200000L
#define SQL_FN_NUM_ROUND                    0x00400000L
#define SQL_FN_NUM_TRUNCATE                 0x00800000L

#if (ODBCVER >= 0x0300)
#define SQL_SNVF_BIT_LENGTH				0x00000001L
#define SQL_SNVF_CHAR_LENGTH				0x00000002L
#define SQL_SNVF_CHARACTER_LENGTH			0x00000004L
#define SQL_SNVF_EXTRACT				0x00000008L
#define SQL_SNVF_OCTET_LENGTH				0x00000010L
#define SQL_SNVF_POSITION				0x00000020L
#endif

#define SQL_FN_TD_NOW                       0x00000001L
#define SQL_FN_TD_CURDATE                   0x00000002L
#define SQL_FN_TD_DAYOFMONTH                0x00000004L
#define SQL_FN_TD_DAYOFWEEK                 0x00000008L
#define SQL_FN_TD_DAYOFYEAR                 0x00000010L
#define SQL_FN_TD_MONTH                     0x00000020L
#define SQL_FN_TD_QUARTER                   0x00000040L
#define SQL_FN_TD_WEEK                      0x00000080L
#define SQL_FN_TD_YEAR                      0x00000100L
#define SQL_FN_TD_CURTIME                   0x00000200L
#define SQL_FN_TD_HOUR                      0x00000400L
#define SQL_FN_TD_MINUTE                    0x00000800L
#define SQL_FN_TD_SECOND                    0x00001000L
#define SQL_FN_TD_TIMESTAMPADD              0x00002000L
#define SQL_FN_TD_TIMESTAMPDIFF             0x00004000L
#define SQL_FN_TD_DAYNAME                   0x00008000L
#define SQL_FN_TD_MONTHNAME                 0x00010000L
#if (ODBCVER >= 0x0300)
#define SQL_FN_TD_CURRENT_DATE				0x00020000L
#define SQL_FN_TD_CURRENT_TIME				0x00040000L
#define SQL_FN_TD_CURRENT_TIMESTAMP			0x00080000L
#define SQL_FN_TD_EXTRACT				0x00100000L
#endif

#if (ODBCVER >= 0x0300)
#define SQL_SDF_CURRENT_DATE				0x00000001L
#define SQL_SDF_CURRENT_TIME				0x00000002L
#define SQL_SDF_CURRENT_TIMESTAMP			0x00000004L
#endif

#define SQL_FN_SYS_USERNAME                 0x00000001L
#define SQL_FN_SYS_DBNAME                   0x00000002L
#define SQL_FN_SYS_IFNULL                   0x00000004L

#define SQL_FN_TSI_FRAC_SECOND              0x00000001L
#define SQL_FN_TSI_SECOND                   0x00000002L
#define SQL_FN_TSI_MINUTE                   0x00000004L
#define SQL_FN_TSI_HOUR                     0x00000008L
#define SQL_FN_TSI_DAY                      0x00000010L
#define SQL_FN_TSI_WEEK                     0x00000020L
#define SQL_FN_TSI_MONTH                    0x00000040L
#define SQL_FN_TSI_QUARTER                  0x00000080L
#define SQL_FN_TSI_YEAR                     0x00000100L

#if (ODBCVER >= 0x0300)
#define SQL_CA1_NEXT					0x00000001L
#define SQL_CA1_ABSOLUTE				0x00000002L
#define SQL_CA1_RELATIVE				0x00000004L
#define SQL_CA1_BOOKMARK				0x00000008L

#define SQL_CA1_LOCK_NO_CHANGE				0x00000040L
#define SQL_CA1_LOCK_EXCLUSIVE				0x00000080L
#define SQL_CA1_LOCK_UNLOCK				0x00000100L

#define SQL_CA1_POS_POSITION				0x00000200L
#define SQL_CA1_POS_UPDATE				0x00000400L
#define SQL_CA1_POS_DELETE				0x00000800L
#define SQL_CA1_POS_REFRESH				0x00001000L

#define SQL_CA1_POSITIONED_UPDATE			0x00002000L
#define SQL_CA1_POSITIONED_DELETE			0x00004000L
#define SQL_CA1_SELECT_FOR_UPDATE			0x00008000L

#define SQL_CA1_BULK_ADD			0x00010000L
#define SQL_CA1_BULK_UPDATE_BY_BOOKMARK		0x00020000L
#define SQL_CA1_BULK_DELETE_BY_BOOKMARK		0x00040000L
#define SQL_CA1_BULK_FETCH_BY_BOOKMARK		0x00080000L
#endif

#if (ODBCVER >= 0x0300)
#define SQL_CA2_READ_ONLY_CONCURRENCY		0x00000001L
#define SQL_CA2_LOCK_CONCURRENCY		0x00000002L
#define SQL_CA2_OPT_ROWVER_CONCURRENCY		0x00000004L
#define SQL_CA2_OPT_VALUES_CONCURRENCY		0x00000008L

#define SQL_CA2_SENSITIVITY_ADDITIONS		0x00000010L
#define SQL_CA2_SENSITIVITY_DELETIONS		0x00000020L
#define SQL_CA2_SENSITIVITY_UPDATES		0x00000040L

#define SQL_CA2_MAX_ROWS_SELECT			0x00000080L
#define SQL_CA2_MAX_ROWS_INSERT			0x00000100L
#define SQL_CA2_MAX_ROWS_DELETE			0x00000200L
#define SQL_CA2_MAX_ROWS_UPDATE			0x00000400L
#define SQL_CA2_MAX_ROWS_CATALOG		0x00000800L
#define SQL_CA2_MAX_ROWS_AFFECTS_ALL		(SQL_CA2_MAX_ROWS_SELECT | SQL_CA2_MAX_ROWS_INSERT | SQL_CA2_MAX_ROWS_DELETE | SQL_CA2_MAX_ROWS_UPDATE | SQL_CA2_MAX_ROWS_CATALOG)

#define SQL_CA2_CRC_EXACT			0x00001000L
#define SQL_CA2_CRC_APPROXIMATE			0x00002000L

#define SQL_CA2_SIMULATE_NON_UNIQUE		0x00004000L
#define SQL_CA2_SIMULATE_TRY_UNIQUE		0x00008000L
#define SQL_CA2_SIMULATE_UNIQUE			0x00010000L
#endif

#define SQL_OAC_NONE                        0x0000
#define SQL_OAC_LEVEL1                      0x0001
#define SQL_OAC_LEVEL2                      0x0002

#define SQL_OSCC_NOT_COMPLIANT              0x0000
#define SQL_OSCC_COMPLIANT                  0x0001

#define SQL_OSC_MINIMUM                     0x0000
#define SQL_OSC_CORE                        0x0001
#define SQL_OSC_EXTENDED                    0x0002

#define SQL_CB_NULL                         0x0000
#define SQL_CB_NON_NULL                     0x0001

#define SQL_SO_FORWARD_ONLY                 0x00000001L
#define SQL_SO_KEYSET_DRIVEN                0x00000002L
#define SQL_SO_DYNAMIC                      0x00000004L
#define SQL_SO_MIXED                        0x00000008L
#define SQL_SO_STATIC                       0x00000010L

#define SQL_FD_FETCH_BOOKMARK               0x00000080L

#define SQL_CN_NONE                         0x0000
#define SQL_CN_DIFFERENT                    0x0001
#define SQL_CN_ANY                          0x0002

#define SQL_NNC_NULL                        0x0000
#define SQL_NNC_NON_NULL                    0x0001

#define SQL_NC_START                        0x0002
#define SQL_NC_END                          0x0004

#define SQL_FILE_NOT_SUPPORTED              0x0000
#define SQL_FILE_TABLE                      0x0001
#define SQL_FILE_QUALIFIER                  0x0002
#define SQL_FILE_CATALOG		SQL_FILE_QUALIFIER

#define SQL_GD_BLOCK                        0x00000004L
#define SQL_GD_BOUND                        0x00000008L

#define SQL_PS_POSITIONED_DELETE            0x00000001L
#define SQL_PS_POSITIONED_UPDATE            0x00000002L
#define SQL_PS_SELECT_FOR_UPDATE            0x00000004L

#define SQL_GB_NOT_SUPPORTED                0x0000
#define SQL_GB_GROUP_BY_EQUALS_SELECT       0x0001
#define SQL_GB_GROUP_BY_CONTAINS_SELECT     0x0002
#define SQL_GB_NO_RELATION                  0x0003
#if (ODBCVER >= 0x0300)
#define	SQL_GB_COLLATE			0x0004

#endif

#define SQL_OU_DML_STATEMENTS               0x00000001L
#define SQL_OU_PROCEDURE_INVOCATION         0x00000002L
#define SQL_OU_TABLE_DEFINITION             0x00000004L
#define SQL_OU_INDEX_DEFINITION             0x00000008L
#define SQL_OU_PRIVILEGE_DEFINITION         0x00000010L

#if (ODBCVER >= 0x0300)
#define SQL_SU_DML_STATEMENTS			SQL_OU_DML_STATEMENTS
#define SQL_SU_PROCEDURE_INVOCATION		SQL_OU_PROCEDURE_INVOCATION
#define SQL_SU_TABLE_DEFINITION			SQL_OU_TABLE_DEFINITION
#define SQL_SU_INDEX_DEFINITION			SQL_OU_INDEX_DEFINITION
#define SQL_SU_PRIVILEGE_DEFINITION		SQL_OU_PRIVILEGE_DEFINITION
#endif

#define SQL_QU_DML_STATEMENTS               0x00000001L
#define SQL_QU_PROCEDURE_INVOCATION         0x00000002L
#define SQL_QU_TABLE_DEFINITION             0x00000004L
#define SQL_QU_INDEX_DEFINITION             0x00000008L
#define SQL_QU_PRIVILEGE_DEFINITION         0x00000010L

#if (ODBCVER >= 0x0300)
#define SQL_CU_DML_STATEMENTS			SQL_QU_DML_STATEMENTS
#define SQL_CU_PROCEDURE_INVOCATION		SQL_QU_PROCEDURE_INVOCATION
#define SQL_CU_TABLE_DEFINITION			SQL_QU_TABLE_DEFINITION
#define SQL_CU_INDEX_DEFINITION			SQL_QU_INDEX_DEFINITION
#define SQL_CU_PRIVILEGE_DEFINITION		SQL_QU_PRIVILEGE_DEFINITION
#endif

#define SQL_SQ_COMPARISON                   0x00000001L
#define SQL_SQ_EXISTS                       0x00000002L
#define SQL_SQ_IN                           0x00000004L
#define SQL_SQ_QUANTIFIED                   0x00000008L
#define SQL_SQ_CORRELATED_SUBQUERIES        0x00000010L

#define SQL_U_UNION                         0x00000001L
#define SQL_U_UNION_ALL                     0x00000002L

#define SQL_BP_CLOSE                        0x00000001L
#define SQL_BP_DELETE                       0x00000002L
#define SQL_BP_DROP                         0x00000004L
#define SQL_BP_TRANSACTION                  0x00000008L
#define SQL_BP_UPDATE                       0x00000010L
#define SQL_BP_OTHER_HSTMT                  0x00000020L
#define SQL_BP_SCROLL                       0x00000040L

#define SQL_SS_ADDITIONS                    0x00000001L
#define SQL_SS_DELETIONS                    0x00000002L
#define SQL_SS_UPDATES                      0x00000004L

#define	SQL_CV_CREATE_VIEW					0x00000001L
#define	SQL_CV_CHECK_OPTION					0x00000002L
#define	SQL_CV_CASCADED						0x00000004L
#define	SQL_CV_LOCAL						0x00000008L

#define SQL_LCK_NO_CHANGE                   0x00000001L
#define SQL_LCK_EXCLUSIVE                   0x00000002L
#define SQL_LCK_UNLOCK                      0x00000004L

#define SQL_POS_POSITION                    0x00000001L
#define SQL_POS_REFRESH                     0x00000002L
#define SQL_POS_UPDATE                      0x00000004L
#define SQL_POS_DELETE                      0x00000008L
#define SQL_POS_ADD                         0x00000010L

#define SQL_QL_START                        0x0001
#define SQL_QL_END                          0x0002

#if (ODBCVER >= 0x0300)
#define SQL_AF_AVG						0x00000001L
#define SQL_AF_COUNT					0x00000002L
#define SQL_AF_MAX						0x00000004L
#define SQL_AF_MIN						0x00000008L
#define SQL_AF_SUM						0x00000010L
#define SQL_AF_DISTINCT					0x00000020L
#define SQL_AF_ALL						0x00000040L

#define	SQL_SC_SQL92_ENTRY				0x00000001L
#define	SQL_SC_FIPS127_2_TRANSITIONAL	0x00000002L
#define	SQL_SC_SQL92_INTERMEDIATE		0x00000004L
#define	SQL_SC_SQL92_FULL				0x00000008L

#define SQL_DL_SQL92_DATE						0x00000001L
#define SQL_DL_SQL92_TIME						0x00000002L
#define SQL_DL_SQL92_TIMESTAMP					0x00000004L
#define SQL_DL_SQL92_INTERVAL_YEAR				0x00000008L
#define SQL_DL_SQL92_INTERVAL_MONTH				0x00000010L
#define SQL_DL_SQL92_INTERVAL_DAY				0x00000020L
#define SQL_DL_SQL92_INTERVAL_HOUR				0x00000040L
#define	SQL_DL_SQL92_INTERVAL_MINUTE			0x00000080L
#define SQL_DL_SQL92_INTERVAL_SECOND			0x00000100L
#define SQL_DL_SQL92_INTERVAL_YEAR_TO_MONTH		0x00000200L
#define SQL_DL_SQL92_INTERVAL_DAY_TO_HOUR		0x00000400L
#define SQL_DL_SQL92_INTERVAL_DAY_TO_MINUTE		0x00000800L
#define SQL_DL_SQL92_INTERVAL_DAY_TO_SECOND		0x00001000L
#define SQL_DL_SQL92_INTERVAL_HOUR_TO_MINUTE	0x00002000L
#define SQL_DL_SQL92_INTERVAL_HOUR_TO_SECOND	0x00004000L
#define SQL_DL_SQL92_INTERVAL_MINUTE_TO_SECOND	0x00008000L

#define SQL_CL_START						SQL_QL_START
#define SQL_CL_END							SQL_QL_END

#define SQL_BRC_PROCEDURES			0x0000001
#define	SQL_BRC_EXPLICIT			0x0000002
#define	SQL_BRC_ROLLED_UP			0x0000004

#define SQL_BS_SELECT_EXPLICIT				0x00000001L
#define SQL_BS_ROW_COUNT_EXPLICIT			0x00000002L
#define SQL_BS_SELECT_PROC					0x00000004L
#define SQL_BS_ROW_COUNT_PROC				0x00000008L

#define SQL_PARC_BATCH		1
#define SQL_PARC_NO_BATCH	2

#define SQL_PAS_BATCH				1
#define SQL_PAS_NO_BATCH			2
#define SQL_PAS_NO_SELECT			3

#define SQL_IK_NONE							0x00000000L
#define SQL_IK_ASC							0x00000001L
#define SQL_IK_DESC							0x00000002L
#define SQL_IK_ALL							(SQL_IK_ASC | SQL_IK_DESC)

#define SQL_ISV_ASSERTIONS					0x00000001L
#define SQL_ISV_CHARACTER_SETS				0x00000002L
#define SQL_ISV_CHECK_CONSTRAINTS			0x00000004L
#define SQL_ISV_COLLATIONS					0x00000008L
#define SQL_ISV_COLUMN_DOMAIN_USAGE			0x00000010L
#define SQL_ISV_COLUMN_PRIVILEGES			0x00000020L
#define SQL_ISV_COLUMNS						0x00000040L
#define SQL_ISV_CONSTRAINT_COLUMN_USAGE		0x00000080L
#define SQL_ISV_CONSTRAINT_TABLE_USAGE		0x00000100L
#define SQL_ISV_DOMAIN_CONSTRAINTS			0x00000200L
#define SQL_ISV_DOMAINS						0x00000400L
#define SQL_ISV_KEY_COLUMN_USAGE			0x00000800L
#define SQL_ISV_REFERENTIAL_CONSTRAINTS		0x00001000L
#define SQL_ISV_SCHEMATA					0x00002000L
#define SQL_ISV_SQL_LANGUAGES				0x00004000L
#define	SQL_ISV_TABLE_CONSTRAINTS			0x00008000L
#define SQL_ISV_TABLE_PRIVILEGES			0x00010000L
#define SQL_ISV_TABLES						0x00020000L
#define SQL_ISV_TRANSLATIONS				0x00040000L
#define SQL_ISV_USAGE_PRIVILEGES			0x00080000L
#define SQL_ISV_VIEW_COLUMN_USAGE			0x00100000L
#define SQL_ISV_VIEW_TABLE_USAGE			0x00200000L
#define SQL_ISV_VIEWS						0x00400000L

#define	SQL_AM_NONE			0
#define	SQL_AM_CONNECTION	1
#define	SQL_AM_STATEMENT	2

#define SQL_AD_CONSTRAINT_NAME_DEFINITION			0x00000001L
#define	SQL_AD_ADD_DOMAIN_CONSTRAINT	 			0x00000002L
#define	SQL_AD_DROP_DOMAIN_CONSTRAINT	 			0x00000004L
#define	SQL_AD_ADD_DOMAIN_DEFAULT   	 			0x00000008L
#define	SQL_AD_DROP_DOMAIN_DEFAULT   	 			0x00000010L
#define SQL_AD_ADD_CONSTRAINT_INITIALLY_DEFERRED	0x00000020L
#define SQL_AD_ADD_CONSTRAINT_INITIALLY_IMMEDIATE	0x00000040L
#define SQL_AD_ADD_CONSTRAINT_DEFERRABLE			0x00000080L
#define SQL_AD_ADD_CONSTRAINT_NON_DEFERRABLE		0x00000100L

#define	SQL_CS_CREATE_SCHEMA				0x00000001L
#define	SQL_CS_AUTHORIZATION				0x00000002L
#define	SQL_CS_DEFAULT_CHARACTER_SET		0x00000004L

#define	SQL_CTR_CREATE_TRANSLATION			0x00000001L

#define	SQL_CA_CREATE_ASSERTION					0x00000001L
#define	SQL_CA_CONSTRAINT_INITIALLY_DEFERRED	0x00000010L
#define	SQL_CA_CONSTRAINT_INITIALLY_IMMEDIATE	0x00000020L
#define	SQL_CA_CONSTRAINT_DEFERRABLE			0x00000040L
#define	SQL_CA_CONSTRAINT_NON_DEFERRABLE		0x00000080L

#define	SQL_CCS_CREATE_CHARACTER_SET		0x00000001L
#define	SQL_CCS_COLLATE_CLAUSE				0x00000002L
#define	SQL_CCS_LIMITED_COLLATION			0x00000004L

#define	SQL_CCOL_CREATE_COLLATION			0x00000001L

#define	SQL_CDO_CREATE_DOMAIN					0x00000001L
#define	SQL_CDO_DEFAULT							0x00000002L
#define	SQL_CDO_CONSTRAINT						0x00000004L
#define	SQL_CDO_COLLATION						0x00000008L
#define SQL_CDO_CONSTRAINT_NAME_DEFINITION		0x00000010L
#define SQL_CDO_CONSTRAINT_INITIALLY_DEFERRED	0x00000020L
#define SQL_CDO_CONSTRAINT_INITIALLY_IMMEDIATE	0x00000040L
#define SQL_CDO_CONSTRAINT_DEFERRABLE			0x00000080L
#define SQL_CDO_CONSTRAINT_NON_DEFERRABLE		0x00000100L

#define	SQL_CT_CREATE_TABLE						0x00000001L
#define	SQL_CT_COMMIT_PRESERVE					0x00000002L
#define	SQL_CT_COMMIT_DELETE					0x00000004L
#define	SQL_CT_GLOBAL_TEMPORARY					0x00000008L
#define	SQL_CT_LOCAL_TEMPORARY					0x00000010L
#define	SQL_CT_CONSTRAINT_INITIALLY_DEFERRED	0x00000020L
#define	SQL_CT_CONSTRAINT_INITIALLY_IMMEDIATE	0x00000040L
#define	SQL_CT_CONSTRAINT_DEFERRABLE			0x00000080L
#define	SQL_CT_CONSTRAINT_NON_DEFERRABLE		0x00000100L
#define SQL_CT_COLUMN_CONSTRAINT				0x00000200L
#define SQL_CT_COLUMN_DEFAULT					0x00000400L
#define SQL_CT_COLUMN_COLLATION					0x00000800L
#define SQL_CT_TABLE_CONSTRAINT					0x00001000L
#define SQL_CT_CONSTRAINT_NAME_DEFINITION		0x00002000L

#define SQL_DI_CREATE_INDEX						0x00000001L
#define SQL_DI_DROP_INDEX						0x00000002L

#define	SQL_DC_DROP_COLLATION					0x00000001L

#define	SQL_DD_DROP_DOMAIN						0x00000001L
#define	SQL_DD_RESTRICT							0x00000002L
#define	SQL_DD_CASCADE							0x00000004L

#define	SQL_DS_DROP_SCHEMA						0x00000001L
#define SQL_DS_RESTRICT							0x00000002L
#define	SQL_DS_CASCADE							0x00000004L

#define	SQL_DCS_DROP_CHARACTER_SET				0x00000001L

#define	SQL_DA_DROP_ASSERTION					0x00000001L

#define	SQL_DT_DROP_TABLE						0x00000001L
#define	SQL_DT_RESTRICT							0x00000002L
#define	SQL_DT_CASCADE							0x00000004L

#define	SQL_DTR_DROP_TRANSLATION				0x00000001L

#define	SQL_DV_DROP_VIEW						0x00000001L
#define	SQL_DV_RESTRICT							0x00000002L
#define	SQL_DV_CASCADE							0x00000004L

#define	SQL_IS_INSERT_LITERALS					0x00000001L
#define SQL_IS_INSERT_SEARCHED					0x00000002L
#define SQL_IS_SELECT_INTO						0x00000004L

#define SQL_OIC_CORE							1UL
#define SQL_OIC_LEVEL1							2UL
#define SQL_OIC_LEVEL2							3UL

#define SQL_SFKD_CASCADE						0x00000001L
#define SQL_SFKD_NO_ACTION						0x00000002L
#define SQL_SFKD_SET_DEFAULT					0x00000004L
#define SQL_SFKD_SET_NULL						0x00000008L

#define SQL_SFKU_CASCADE						0x00000001L
#define SQL_SFKU_NO_ACTION						0x00000002L
#define SQL_SFKU_SET_DEFAULT					0x00000004L
#define SQL_SFKU_SET_NULL						0x00000008L

#define SQL_SG_USAGE_ON_DOMAIN					0x00000001L
#define SQL_SG_USAGE_ON_CHARACTER_SET			0x00000002L
#define SQL_SG_USAGE_ON_COLLATION				0x00000004L
#define SQL_SG_USAGE_ON_TRANSLATION				0x00000008L
#define SQL_SG_WITH_GRANT_OPTION				0x00000010L
#define SQL_SG_DELETE_TABLE						0x00000020L
#define SQL_SG_INSERT_TABLE						0x00000040L
#define SQL_SG_INSERT_COLUMN					0x00000080L
#define SQL_SG_REFERENCES_TABLE					0x00000100L
#define SQL_SG_REFERENCES_COLUMN				0x00000200L
#define SQL_SG_SELECT_TABLE						0x00000400L
#define SQL_SG_UPDATE_TABLE						0x00000800L
#define SQL_SG_UPDATE_COLUMN					0x00001000L

#define SQL_SP_EXISTS							0x00000001L
#define SQL_SP_ISNOTNULL						0x00000002L
#define SQL_SP_ISNULL							0x00000004L
#define SQL_SP_MATCH_FULL						0x00000008L
#define SQL_SP_MATCH_PARTIAL					0x00000010L
#define SQL_SP_MATCH_UNIQUE_FULL				0x00000020L
#define SQL_SP_MATCH_UNIQUE_PARTIAL				0x00000040L
#define SQL_SP_OVERLAPS							0x00000080L
#define SQL_SP_UNIQUE							0x00000100L
#define SQL_SP_LIKE								0x00000200L
#define SQL_SP_IN								0x00000400L
#define SQL_SP_BETWEEN							0x00000800L
#define SQL_SP_COMPARISON						0x00001000L
#define SQL_SP_QUANTIFIED_COMPARISON			0x00002000L

#define SQL_SRJO_CORRESPONDING_CLAUSE			0x00000001L
#define SQL_SRJO_CROSS_JOIN						0x00000002L
#define SQL_SRJO_EXCEPT_JOIN					0x00000004L
#define SQL_SRJO_FULL_OUTER_JOIN				0x00000008L
#define SQL_SRJO_INNER_JOIN						0x00000010L
#define SQL_SRJO_INTERSECT_JOIN					0x00000020L
#define SQL_SRJO_LEFT_OUTER_JOIN				0x00000040L
#define SQL_SRJO_NATURAL_JOIN					0x00000080L
#define SQL_SRJO_RIGHT_OUTER_JOIN				0x00000100L
#define SQL_SRJO_UNION_JOIN						0x00000200L

#define SQL_SR_USAGE_ON_DOMAIN					0x00000001L
#define SQL_SR_USAGE_ON_CHARACTER_SET			0x00000002L
#define SQL_SR_USAGE_ON_COLLATION				0x00000004L
#define SQL_SR_USAGE_ON_TRANSLATION				0x00000008L
#define SQL_SR_GRANT_OPTION_FOR					0x00000010L
#define SQL_SR_CASCADE							0x00000020L
#define SQL_SR_RESTRICT							0x00000040L
#define SQL_SR_DELETE_TABLE						0x00000080L
#define SQL_SR_INSERT_TABLE						0x00000100L
#define SQL_SR_INSERT_COLUMN					0x00000200L
#define SQL_SR_REFERENCES_TABLE					0x00000400L
#define SQL_SR_REFERENCES_COLUMN				0x00000800L
#define SQL_SR_SELECT_TABLE						0x00001000L
#define SQL_SR_UPDATE_TABLE						0x00002000L
#define SQL_SR_UPDATE_COLUMN					0x00004000L

#define SQL_SRVC_VALUE_EXPRESSION				0x00000001L
#define SQL_SRVC_NULL							0x00000002L
#define SQL_SRVC_DEFAULT						0x00000004L
#define SQL_SRVC_ROW_SUBQUERY					0x00000008L

#define SQL_SVE_CASE							0x00000001L
#define SQL_SVE_CAST							0x00000002L
#define SQL_SVE_COALESCE						0x00000004L
#define SQL_SVE_NULLIF							0x00000008L

#define SQL_SCC_XOPEN_CLI_VERSION1				0x00000001L
#define SQL_SCC_ISO92_CLI						0x00000002L

#define SQL_US_UNION							SQL_U_UNION
#define SQL_US_UNION_ALL						SQL_U_UNION_ALL

#endif

#define SQL_DTC_ENLIST_EXPENSIVE				0x00000001L
#define SQL_DTC_UNENLIST_EXPENSIVE				0x00000002L

#if (ODBCVER >= 0x0300)
#define SQL_FETCH_FIRST_USER				31
#define SQL_FETCH_FIRST_SYSTEM				32
#endif

#define SQL_ENTIRE_ROWSET            0

#define SQL_POSITION                 0
#define SQL_REFRESH                  1
#define SQL_UPDATE                   2
#define SQL_DELETE                   3

#define SQL_ADD                      4
#define	SQL_SETPOS_MAX_OPTION_VALUE			SQL_ADD
#if (ODBCVER >= 0x0300)
#define SQL_UPDATE_BY_BOOKMARK		 5
#define SQL_DELETE_BY_BOOKMARK		 6
#define	SQL_FETCH_BY_BOOKMARK		 7

#endif

#define SQL_LOCK_NO_CHANGE           0
#define SQL_LOCK_EXCLUSIVE           1
#define SQL_LOCK_UNLOCK              2

#define	SQL_SETPOS_MAX_LOCK_VALUE		SQL_LOCK_UNLOCK

#define SQL_POSITION_TO(hstmt,irow) SQLSetPos(hstmt,irow,SQL_POSITION,SQL_LOCK_NO_CHANGE)
#define SQL_LOCK_RECORD(hstmt,irow,fLock) SQLSetPos(hstmt,irow,SQL_POSITION,fLock)
#define SQL_REFRESH_RECORD(hstmt,irow,fLock) SQLSetPos(hstmt,irow,SQL_REFRESH,fLock)
#define SQL_UPDATE_RECORD(hstmt,irow) SQLSetPos(hstmt,irow,SQL_UPDATE,SQL_LOCK_NO_CHANGE)
#define SQL_DELETE_RECORD(hstmt,irow) SQLSetPos(hstmt,irow,SQL_DELETE,SQL_LOCK_NO_CHANGE)
#define SQL_ADD_RECORD(hstmt,irow) SQLSetPos(hstmt,irow,SQL_ADD,SQL_LOCK_NO_CHANGE)

#define SQL_BEST_ROWID                  1
#define SQL_ROWVER                      2

#define SQL_PC_NOT_PSEUDO               1

#define SQL_QUICK                       0
#define SQL_ENSURE                      1

#define SQL_TABLE_STAT                  0

#if (ODBCVER >= 0x0300)
#define SQL_ALL_CATALOGS				"%"
#define SQL_ALL_SCHEMAS					"%"
#define SQL_ALL_TABLE_TYPES				"%"
#endif  /* ODBCVER >= 0x0300 */

#define SQL_DRIVER_NOPROMPT             0
#define SQL_DRIVER_COMPLETE             1
#define SQL_DRIVER_PROMPT               2
#define SQL_DRIVER_COMPLETE_REQUIRED    3

SQLRETURN WINAPI  SQLDriverConnect(
    SQLHDBC            hdbc,
    SQLHWND            hwnd,
    SQLCHAR 		  *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLCHAR           *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT 	  *pcbConnStrOut,
    SQLUSMALLINT       fDriverCompletion);

#define SQL_FETCH_BOOKMARK               8

#define SQL_ROW_SUCCESS                  0
#define SQL_ROW_DELETED                  1
#define SQL_ROW_UPDATED                  2
#define SQL_ROW_NOROW                    3
#define SQL_ROW_ADDED                    4
#define SQL_ROW_ERROR                    5
#if (ODBCVER >= 0x0300)
#define SQL_ROW_SUCCESS_WITH_INFO		 6
#define SQL_ROW_PROCEED					 0
#define SQL_ROW_IGNORE					 1
#endif

#if (ODBCVER >= 0x0300)
#define SQL_PARAM_SUCCESS				0
#define SQL_PARAM_SUCCESS_WITH_INFO		6
#define SQL_PARAM_ERROR					5
#define SQL_PARAM_UNUSED				7
#define SQL_PARAM_DIAG_UNAVAILABLE		1

#define SQL_PARAM_PROCEED				0
#define SQL_PARAM_IGNORE				1
#endif

#define SQL_CASCADE                      0
#define SQL_RESTRICT                     1
#define SQL_SET_NULL                     2
#if (ODBCVER >= 0x0250)
#define SQL_NO_ACTION			 3
#define SQL_SET_DEFAULT			 4
#endif

#if (ODBCVER >= 0x0300)

#define SQL_INITIALLY_DEFERRED			5
#define SQL_INITIALLY_IMMEDIATE			6
#define SQL_NOT_DEFERRABLE			7

#endif

#define SQL_PARAM_TYPE_UNKNOWN           0
#define SQL_PARAM_INPUT                  1
#define SQL_PARAM_INPUT_OUTPUT           2
#define SQL_RESULT_COL                   3
#define SQL_PARAM_OUTPUT                 4
#define SQL_RETURN_VALUE                 5

#define SQL_PT_UNKNOWN                   0
#define SQL_PT_PROCEDURE                 1
#define SQL_PT_FUNCTION                  2

#define SQL_ODBC_KEYWORDS "ABSOLUTE,ACTION,ADA,ADD,ALL,ALLOCATE,ALTER,AND,ANY,ARE,AS,"

SQLRETURN WINAPI  SQLBrowseConnect(
    SQLHDBC            hdbc,
    SQLCHAR 		  *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLCHAR 		  *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT       *pcbConnStrOut);

#if (ODBCVER >= 0x0300)
SQLRETURN WINAPI	SQLBulkOperations(
	SQLHSTMT			StatementHandle,
	SQLSMALLINT			Operation);
#endif

SQLRETURN WINAPI  SQLColAttributes(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       icol,
    SQLUSMALLINT       fDescType,
    SQLPOINTER         rgbDesc,
    SQLSMALLINT        cbDescMax,
    SQLSMALLINT 	  *pcbDesc,
    SQLINTEGER 		  *pfDesc);

SQLRETURN WINAPI  SQLColumnPrivileges(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName,
    SQLCHAR 		  *szColumnName,
    SQLSMALLINT        cbColumnName);

SQLRETURN WINAPI  SQLDescribeParam(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       ipar,
    SQLSMALLINT 	  *pfSqlType,
    SQLUINTEGER 	  *pcbParamDef,
    SQLSMALLINT 	  *pibScale,
    SQLSMALLINT 	  *pfNullable);

SQLRETURN WINAPI  SQLExtendedFetch(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       fFetchType,
    SQLINTEGER         irow,
    SQLUINTEGER 	  *pcrow,
    SQLUSMALLINT 	  *rgfRowStatus);

SQLRETURN WINAPI  SQLForeignKeys(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szPkCatalogName,
    SQLSMALLINT        cbPkCatalogName,
    SQLCHAR 		  *szPkSchemaName,
    SQLSMALLINT        cbPkSchemaName,
    SQLCHAR 		  *szPkTableName,
    SQLSMALLINT        cbPkTableName,
    SQLCHAR 		  *szFkCatalogName,
    SQLSMALLINT        cbFkCatalogName,
    SQLCHAR 		  *szFkSchemaName,
    SQLSMALLINT        cbFkSchemaName,
    SQLCHAR 		  *szFkTableName,
    SQLSMALLINT        cbFkTableName);

SQLRETURN WINAPI  SQLMoreResults(
    SQLHSTMT           hstmt);

SQLRETURN WINAPI  SQLNativeSql(
    SQLHDBC            hdbc,
    SQLCHAR 		  *szSqlStrIn,
    SQLINTEGER         cbSqlStrIn,
    SQLCHAR 		  *szSqlStr,
    SQLINTEGER         cbSqlStrMax,
    SQLINTEGER 		  *pcbSqlStr);

SQLRETURN WINAPI  SQLNumParams(
    SQLHSTMT           hstmt,
    SQLSMALLINT 	  *pcpar);

SQLRETURN WINAPI  SQLParamOptions(
    SQLHSTMT           hstmt,
    SQLUINTEGER        crow,
    SQLUINTEGER 	  *pirow);

SQLRETURN WINAPI  SQLPrimaryKeys(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName);

SQLRETURN WINAPI  SQLProcedureColumns(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLCHAR 		  *szProcName,
    SQLSMALLINT        cbProcName,
    SQLCHAR 		  *szColumnName,
    SQLSMALLINT        cbColumnName);

SQLRETURN WINAPI  SQLProcedures(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLCHAR 		  *szProcName,
    SQLSMALLINT        cbProcName);

SQLRETURN WINAPI  SQLSetPos(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       irow,
    SQLUSMALLINT       fOption,
    SQLUSMALLINT       fLock);

SQLRETURN WINAPI  SQLTablePrivileges(
    SQLHSTMT           hstmt,
    SQLCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName);

SQLRETURN WINAPI  SQLDrivers(
    SQLHENV            henv,
    SQLUSMALLINT       fDirection,
    SQLCHAR 		  *szDriverDesc,
    SQLSMALLINT        cbDriverDescMax,
    SQLSMALLINT 	  *pcbDriverDesc,
    SQLCHAR 		  *szDriverAttributes,
    SQLSMALLINT        cbDrvrAttrMax,
    SQLSMALLINT 	  *pcbDrvrAttr);

SQLRETURN WINAPI  SQLBindParameter(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       ipar,
    SQLSMALLINT        fParamType,
    SQLSMALLINT        fCType,
    SQLSMALLINT        fSqlType,
    SQLUINTEGER        cbColDef,
    SQLSMALLINT        ibScale,
    SQLPOINTER         rgbValue,
    SQLINTEGER         cbValueMax,
    SQLINTEGER 		  *pcbValue);

#ifdef ODBC_STD
#define SQLAllocHandle  SQLAllocHandleStd
#define SQLAllocEnv(phenv)  SQLAllocHandleStd(SQL_HANDLE_ENV, SQL_NULL_HANDLE, phenv)

#define SQL_YEAR						SQL_CODE_YEAR
#define SQL_MONTH						SQL_CODE_MONTH
#define SQL_DAY							SQL_CODE_DAY
#define SQL_HOUR						SQL_CODE_HOUR
#define SQL_MINUTE						SQL_CODE_MINUTE
#define SQL_SECOND						SQL_CODE_SECOND
#define SQL_YEAR_TO_MONTH				SQL_CODE_YEAR_TO_MONTH
#define SQL_DAY_TO_HOUR					SQL_CODE_DAY_TO_HOUR
#define SQL_DAY_TO_MINUTE				SQL_CODE_DAY_TO_MINUTE
#define SQL_DAY_TO_SECOND				SQL_CODE_DAY_TO_SECOND
#define SQL_HOUR_TO_MINUTE				SQL_CODE_HOUR_TO_MINUTE
#define SQL_HOUR_TO_SECOND				SQL_CODE_HOUR_TO_SECOND
#define SQL_MINUTE_TO_SECOND			SQL_CODE_MINUTE_TO_SECOND
#endif

#if (ODBCVER >= 0x0300)
SQLRETURN WINAPI  SQLAllocHandleStd(
	SQLSMALLINT		fHandleType,
	SQLHANDLE		hInput,
	SQLHANDLE	   *phOutput);
#endif

#define SQL_DATABASE_NAME               16
#define SQL_FD_FETCH_PREV               SQL_FD_FETCH_PRIOR
#define SQL_FETCH_PREV                  SQL_FETCH_PRIOR
#define SQL_CONCUR_TIMESTAMP            SQL_CONCUR_ROWVER
#define SQL_SCCO_OPT_TIMESTAMP          SQL_SCCO_OPT_ROWVER
#define SQL_CC_DELETE                   SQL_CB_DELETE
#define SQL_CR_DELETE                   SQL_CB_DELETE
#define SQL_CC_CLOSE                    SQL_CB_CLOSE
#define SQL_CR_CLOSE                    SQL_CB_CLOSE
#define SQL_CC_PRESERVE                 SQL_CB_PRESERVE
#define SQL_CR_PRESERVE                 SQL_CB_PRESERVE
#define SQL_SCROLL_FORWARD_ONLY         0L
#define SQL_SCROLL_KEYSET_DRIVEN        (-1L)
#define SQL_SCROLL_DYNAMIC              (-2L)
#define SQL_SCROLL_STATIC               (-3L)

SQLRETURN WINAPI  SQLSetScrollOptions(
    SQLHSTMT           hstmt,
    SQLUSMALLINT       fConcurrency,
    SQLINTEGER         crowKeyset,
    SQLUSMALLINT       crowRowset);

#define		TRACE_VERSION	1000

RETCODE	 TraceOpenLogFile(LPWSTR,LPWSTR,DWORD);
RETCODE	 TraceCloseLogFile(void);
VOID	 TraceReturn(RETCODE,RETCODE);
DWORD	 TraceVersion(void);

/*#define TRACE_ON		0x00000001L*/
#define TRACE_VS_EVENT_ON	0x00000002L

RETCODE	TraceVSControl(DWORD);

#define ODBC_VS_FLAG_UNICODE_ARG	0x00000001L
#define	ODBC_VS_FLAG_UNICODE_COR	0x00000002L
#define ODBC_VS_FLAG_RETCODE		0x00000004L
#define ODBC_VS_FLAG_STOP		0x00000008L

typedef struct tagODBC_VS_ARGS {
	const GUID	*pguidEvent;
	DWORD	dwFlags;
	union {
		WCHAR	*wszArg;
		CHAR	*szArg;
	} DUMMYUNIONNAME1;
	union {
		WCHAR	*wszCorrelation;
		CHAR	*szCorrelation;
	} DUMMYUNIONNAME2;
	RETCODE	RetCode;
} ODBC_VS_ARGS, *PODBC_VS_ARGS;

VOID	FireVSDebugEvent(PODBC_VS_ARGS);


#ifdef __cplusplus
}
#endif

#endif
