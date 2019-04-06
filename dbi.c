#include "jsi.h"

#include <string.h>

#include <dbi.h>

#define FQINITIAL 2048
#define FQINC 64

dbi_inst g_dbi;

typedef struct {
    Jsi_Interp    *interp;
    int            objid;
    Jsi_Obj        *fobj;

    dbi_conn    db;
    bool        is_open;
} DBObj;

typedef struct {
    Jsi_Interp    *interp;
    int            objid;
    Jsi_Obj        *fobj;

    DBObj        *dbobj;

    dbi_result    query;
    bool		on_record;

    // Cached info:
    unsigned long long    m_resultCount;
    int                    m_fieldCount;
    unsigned short       *m_fieldTypes;
    char               **m_fieldNames;
} DBQueryObj;

static Jsi_CmdProcDecl(DBSeekCmd)
{
    DBQueryObj *cmdPtr = (DBQueryObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->query )
        return JSI_ERROR;

    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 1 || argc > 1 )
        return JSI_ERROR;

    Jsi_Value *arg = Jsi_ValueArrayIndex(interp, args, 0);
    if( arg == NULL || !Jsi_ValueIsNumber(interp, arg) )
        return JSI_ERROR;

    int vnum = Jsi_ValueToNumberInt(interp, arg, 1);
    int seekres = dbi_result_seek_row( cmdPtr->query, vnum+1 );
    cmdPtr->on_record = (seekres == 1 ? true : false);
    Jsi_ValueMakeBool(interp, ret, seekres);

    return JSI_OK;
}

static Jsi_CmdProcDecl(DBValueCmd)
{
    DBQueryObj *cmdPtr = (DBQueryObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->query )
        return JSI_ERROR;

    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 1 || argc > 1 )
        return JSI_ERROR;

    Jsi_Value *arg = Jsi_ValueArrayIndex(interp, args, 0);
    if( arg == NULL || !Jsi_ValueIsNumber(interp, arg) )
        return JSI_ERROR;

    int idx = Jsi_ValueToNumberInt(interp, arg, 1);
    if( idx < 0 || idx >= cmdPtr->m_fieldCount || !cmdPtr->on_record )
    {
        // TODO: Index out of range
        return JSI_ERROR;
    }

    if( 1 == dbi_result_field_is_null_idx(cmdPtr->query, idx+1) )
    {
        Jsi_ValueMakeNull(interp, ret);
        return JSI_OK;
    }

    switch( cmdPtr->m_fieldTypes[idx] )
    {
        case DBI_TYPE_INTEGER:
        {
            long long num = dbi_result_get_longlong_idx( cmdPtr->query, idx+1 );
            Jsi_ValueMakeNumber(interp, ret, num);
            break;
        }
        case DBI_TYPE_DECIMAL:
        {
            double num = dbi_result_get_double_idx( cmdPtr->query, idx+1 );
            Jsi_ValueMakeNumber(interp, ret, num);
            break;
        }
        case DBI_TYPE_STRING:
        {
            int inLength = dbi_result_get_field_length_idx( cmdPtr->query, idx+1 );
            char *str = dbi_result_get_string_copy_idx( cmdPtr->query, idx+1 );
            Jsi_ValueMakeBlob(interp, ret, (unsigned char *)str, inLength);
            break;
        }
        case DBI_TYPE_BINARY:
        {
            int inLength = dbi_result_get_field_length_idx( cmdPtr->query, idx+1 );
            unsigned char *str = dbi_result_get_binary_copy_idx( cmdPtr->query, idx+1 );
            Jsi_ValueMakeBlob(interp, ret, str, inLength);
            break;
        }
        case DBI_TYPE_DATETIME:
        {
            // just return the seconds.
            long long num = dbi_result_get_datetime_idx( cmdPtr->query, idx+1 );
            Jsi_ValueMakeNumber(interp, ret, num);
            /*
            Jsi_DString dStr;
            Jsi_DSInit(&dStr);
            Jsi_Number num = (Jsi_Number)((Jsi_Wide)(num*1000));
            rc = Jsi_DatetimeFormat(interp, num, "", 0, &dStr);
            Jsi_ValueMakeStringDup(interp, ret, Jsi_DSValue(&dStr));

            Jsi_Value *vstr = Jsi_ValueNew(interp);
            */
            break;
        }
    }

    return JSI_OK;
}

static Jsi_CmdSpec dbQueryCmds[] = {
    { "seek",	DBSeekCmd,	1,	1,	"row:number",		.help="Seek to a specific row", .retType=(uint)JSI_TT_BOOLEAN },
    { "value",	DBValueCmd,	1,	1,	"column:number",	.help="Return the value at the provided field index/name" },
    { NULL, 0,0,0,0, .help="Commands for interacting with a DB Query object"  }
};

static Jsi_RC dbQueryObjFree(Jsi_Interp *interp, void *data)
{
    DBQueryObj *cmdPtr = (DBQueryObj *)data;
    if( cmdPtr->query )
    {
        dbi_result_free( cmdPtr->query );
        
        for( int x=0; x < cmdPtr->m_fieldCount; x++ )
            free( cmdPtr->m_fieldNames[x] );
        Jsi_Free( cmdPtr->m_fieldTypes );
        Jsi_Free( cmdPtr->m_fieldNames );
        
        cmdPtr->m_fieldCount = 0;
        cmdPtr->query = NULL;
    }
    //cmdPtr->interp = NULL;
    Jsi_Free(cmdPtr);
    return JSI_OK;
}

static bool dbQueryObjIsTrue(void *data)
{
    return true;
}

static bool dbQueryObjEqual(void *data1, void *data2)
{
    return (data1 == data2);
}

static Jsi_UserObjReg dbqueryobject = {
    "DBQueryObject",
    dbQueryCmds,
    dbQueryObjFree,
    dbQueryObjIsTrue,
    dbQueryObjEqual
};

static Jsi_CmdProcDecl(DBOpenCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    Jsi_ValueMakeBool(interp, ret, 0);

    if( 0 == dbi_conn_connect(cmdPtr->db) )
    {
        Jsi_ValueMakeBool(interp, ret, 1);
        cmdPtr->is_open = true;
    }

    return JSI_OK;
}

static Jsi_CmdProcDecl(DBErrorCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    const char *errstr;
    int errnum = dbi_conn_error(cmdPtr->db, &errstr);

    Jsi_Value *verrnum = Jsi_ValueNew(interp);
    Jsi_Value *verrstr = Jsi_ValueNew(interp);

    Jsi_ValueMakeNumber(interp, &verrnum, errnum);
    Jsi_ValueMakeStringDup(interp, &verrstr, errstr);

    Jsi_Obj *o = Jsi_ObjNew(interp);
    Jsi_ObjInsert( interp, o, "number", verrnum, JSI_OM_READONLY );
    Jsi_ObjInsert( interp, o, "text", verrstr, JSI_OM_READONLY );
    Jsi_ValueMakeObject(interp, ret, o);

    return JSI_OK;
}

static Jsi_CmdProcDecl(DBBeginCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    int rval = dbi_conn_transaction_begin( cmdPtr->db );
    Jsi_ValueMakeBool(interp, ret, rval == 0 ? 1 : 0);
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBCommitCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    int rval = dbi_conn_transaction_commit( cmdPtr->db );
    Jsi_ValueMakeBool(interp, ret, rval == 0 ? 1 : 0);
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBRollbackCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    int rval = dbi_conn_transaction_rollback( cmdPtr->db );
    Jsi_ValueMakeBool(interp, ret, rval == 0 ? 1 : 0);
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBUseCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    Jsi_ValueMakeBool(interp, ret, 0);

    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 1 || argc > 1 )
        return JSI_ERROR;

    Jsi_Value *argDbname = Jsi_ValueArrayIndex(interp, args, 0);
    if( argDbname == NULL || !Jsi_ValueIsString(interp, argDbname) )
        return JSI_ERROR;

    const char *dbname = Jsi_ValueString(interp, argDbname, NULL);
    int rval = dbi_conn_select_db(cmdPtr->db, dbname);

    Jsi_ValueMakeBool(interp, ret, (rval == 0 ? 1 : 0));
    return JSI_OK;
}

static char *interpolate_query_str(DBObj *db, Jsi_Interp *interp, const char *rawQuery, int qlen, Jsi_Value *tokens)
{
    int argc = Jsi_ValueGetLength(interp, tokens);

    ssize_t fmtCapacity = FQINITIAL;
    char *fmtQuery = (char *)malloc( fmtCapacity+1 );
    int fqPos = 0;

    unsigned int idx;
    char idxStr[6];
    int idxPos = 0;
    bool inIndex = false, atEnd = false;

    for( int x=0; x < qlen; x++ )
    {
        char c = rawQuery[x];
        if( fqPos + 2 >= fmtCapacity ) // +2 because we may have a spare byte + terminator.
        {
            // Enlarge
            fmtCapacity += FQINC;
            fmtQuery = (char *)realloc( fmtQuery, fmtCapacity+1 );
        }

        if( !inIndex )
        {
            if( c == '%' )
            {
                idxPos = 0;
                inIndex = true;
                continue;
            }

            fmtQuery[ fqPos++ ] = c;
            fmtQuery[ fqPos ] = '\0';
            continue;
        }

        // Just an escaped % character:
        if( c == '%' )
        {
            inIndex = false;
            fmtQuery[ fqPos++ ] = '%';
            fmtQuery[ fqPos ] = '\0';
            continue;
        }
        // Still reading an index...
        else if( c >= '0' && c <= '9' && idxPos < 5 )
        {
            idxStr[idxPos++] = c;
            idxStr[idxPos] = '\0';

            // Basically if the query ends on an index, fall through so it processes it.
            // eg; SELECT * FROM table WHERE id=%1 <- query ends while scanning an index.
            if( x+1 != qlen )
                continue;
            atEnd = true;
        }

        inIndex = false;
        idxPos = 0;
        idx = atoi(idxStr); // Start at %1, actually is arg position 1 luckily.

        if( idx <= 0 || idx > argc )
        {
            // Error.
            // TODO: Error!
            free( fmtQuery );
            return NULL;
        }

        Jsi_Value *val = Jsi_ValueArrayIndex(interp, tokens, idx-1);
        if( !val )
        {
            // Error.
            // TODO: Error!
            free( fmtQuery );
            return NULL;
        }

        // String:
        if( Jsi_ValueIsString(interp, val) )
        {
            // Quote it:
            int bufLength;
            char *quoted;
            const char *unquoted = Jsi_ValueString(interp, val, &bufLength);

            // Binary or string?
            //bool isBinary = false;
            //for( unsigned int x=0; x < bufLength && !isBinary; x++ )
                //if( unquoted[x] < 32 || unquoted[x] > 126 ) isBinary = true;

            size_t len;
            //if( isBinary )
                len = dbi_conn_quote_binary_copy( db->db, (const unsigned char*)unquoted, bufLength, (unsigned char**)&quoted );
            //else
            //    len = dbi_conn_quote_string_copy( db->db, unquoted, &quoted );

            if( len == 0 )
            {
                // Error.
                // TODO: Error!
                free( fmtQuery );
                return NULL;
            }
            
            if( len + fqPos >= (unsigned int)fmtCapacity )
            {
                // Enlarge
                fmtCapacity += len + FQINC;
                fmtQuery = (char *)realloc( fmtQuery, fmtCapacity+1 );
            }

            strcat( fmtQuery, quoted );
            free( quoted );
            fqPos += len;
        }
        else if( Jsi_ValueIsNumber(interp, val) )
        {
            int vnum = Jsi_ValueToNumberInt(interp, val, 1);
            double vdub = Jsi_ValueToNumberInt(interp, val, 0);

            char numStr[128];
            if( vnum == vdub )
                snprintf( numStr, 128, "%d", vnum );
            else
                snprintf( numStr, 128, "%f", vdub );
            int len = strlen(numStr);
            
            if( len + fqPos >= fmtCapacity )
            {
                // Enlarge
                fmtCapacity += len + FQINC;
                fmtQuery = (char *)realloc( fmtQuery, fmtCapacity+1 );
            }

            strcat( fmtQuery, numStr );
            fqPos += len;
        }

        // Don't "lose" the character following an index!
        if( !atEnd )
        {
            fmtQuery[ fqPos++ ] = c;
            fmtQuery[ fqPos ] = '\0';
        }
    }

    return fmtQuery;
}

static Jsi_CmdProcDecl(DBQueryCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);

    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 1 || argc > 2 )
        return JSI_ERROR;

    Jsi_Value *argquerystr = Jsi_ValueArrayIndex(interp, args, 0);
    if( argquerystr == NULL || !Jsi_ValueIsString(interp, argquerystr) )
        return JSI_ERROR;

    Jsi_Value *argqueryvals = NULL;
    if( argc > 1 )
    {
        argqueryvals = Jsi_ValueArrayIndex(interp, args, 1);
        if( !Jsi_ValueIsArray(interp, argqueryvals) )
            return JSI_ERROR;
    }

    int qlen;
    const char *qstr = Jsi_ValueString(interp, argquerystr, &qlen);

    // This... is a bit of work:
    dbi_result res = NULL;
    if( argqueryvals != NULL )
    {
        char *final_qstr = interpolate_query_str(cmdPtr, interp, qstr, qlen, argqueryvals);
        res = dbi_conn_query( cmdPtr->db, final_qstr );
        free( final_qstr );
    }
    else
        res = dbi_conn_query( cmdPtr->db, qstr );

    if( !res )
    {
        Jsi_ValueMakeBool(interp, ret, 0);
        return JSI_OK;
    }

    // Make all the things!
    Jsi_Obj *o = Jsi_ObjNew(interp);
    Jsi_PrototypeObjSet(interp, "DBQueryObject", o);
    Jsi_ValueMakeObject(interp, ret, o);

    // Cached data:
    DBQueryObj *qobj = Jsi_Calloc(1, sizeof(DBQueryObj));
    qobj->query = res;
    qobj->dbobj = cmdPtr;
    qobj->on_record = (dbi_result_first_row(res) == 1 ? true : false);
    qobj->m_resultCount = dbi_result_get_numrows( res );
    qobj->m_fieldCount = dbi_result_get_numfields( res );
    qobj->m_fieldTypes = Jsi_Calloc( qobj->m_fieldCount, sizeof(unsigned short) );
    qobj->m_fieldNames = Jsi_Calloc( qobj->m_fieldCount, sizeof(char *) );
    for( int x=0; x < qobj->m_fieldCount; x++ )
    {
        const char *fieldname = dbi_result_get_field_name( res, x+1 );
        qobj->m_fieldTypes[x] = dbi_result_get_field_type_idx( res, x+1 );
        qobj->m_fieldNames[x] = strdup( fieldname );
    }

    // Metadata:
    Jsi_Value *vfields = Jsi_ValueNewArray(interp, (const char **)qobj->m_fieldNames, qobj->m_fieldCount);
    Jsi_ObjInsert( interp, o, "fields", vfields, JSI_OM_READONLY );

    // Result count:
    Jsi_Value *vrowcount = Jsi_ValueNew(interp);
    Jsi_ValueMakeNumber(interp, &vrowcount, qobj->m_resultCount);
    Jsi_ObjInsert( interp, o, "rowcount", vrowcount, JSI_OM_READONLY );

    qobj->fobj = Jsi_ValueGetObj(interp, *ret);
    if ((qobj->objid = Jsi_UserObjNew(interp, &dbqueryobject, qobj->fobj, qobj))<0)
    {
        //printf("ERROR!\n");
        return JSI_ERROR;
    }

    return JSI_OK;
}

static Jsi_CmdProcDecl(DBLastSequenceCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    char *seqname = NULL;
    int argc = Jsi_ValueGetLength(interp, args);
    if( argc > 1 )
        return JSI_ERROR;
    else if( argc == 1 )
    {
        Jsi_Value *vseqname = Jsi_ValueArrayIndex(interp, args, 0);
        if( vseqname == NULL || !Jsi_ValueIsString(interp, vseqname) )
            return JSI_ERROR;
        seqname = Jsi_ValueString(interp, vseqname, NULL);
    }

    unsigned long long num = dbi_conn_sequence_last(cmdPtr->db, seqname);

    Jsi_ValueMakeNumber(interp, ret, num);
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBNextSequenceCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    char *seqname = NULL;
    int argc = Jsi_ValueGetLength(interp, args);
    if( argc > 1 )
        return JSI_ERROR;
    else if( argc == 1 )
    {
        Jsi_Value *vseqname = Jsi_ValueArrayIndex(interp, args, 0);
        if( vseqname == NULL || !Jsi_ValueIsString(interp, vseqname) )
            return JSI_ERROR;
        seqname = Jsi_ValueString(interp, vseqname, NULL);
    }

    unsigned long long num = dbi_conn_sequence_next(cmdPtr->db, seqname);

    Jsi_ValueMakeNumber(interp, ret, num);
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBCheckCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    Jsi_ValueMakeBool(interp, ret, dbi_conn_ping(cmdPtr->db));
    return JSI_OK;
}

static Jsi_CmdProcDecl(DBEscapeCmd)
{
    DBObj *cmdPtr = (DBObj *)Jsi_UserObjGetData(interp, _this, funcPtr);
    if( !cmdPtr->db )
        return JSI_ERROR;

    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 1 || argc > 1 )
        return JSI_ERROR;

    Jsi_Value *vstr = Jsi_ValueArrayIndex(interp, args, 0);
    if( vstr == NULL || !Jsi_ValueIsString(interp, vstr) )
        return JSI_ERROR;

    int bufLength;
    char *quoted;
    const char *unquoted = Jsi_ValueString(interp, vstr, &bufLength);
    int newLength = dbi_conn_quote_binary_copy( cmdPtr->db, (const unsigned char *)unquoted, bufLength, (unsigned char **)&quoted );

    Jsi_ValueMakeBlob(interp, ret, (unsigned char *)quoted, newLength);
    return JSI_OK;
}

static Jsi_CmdSpec dbCmds[] = {
    { "open",        DBOpenCmd,    0,    0,    "",    .help="Establish connection to server and authenticate as necessary", .retType=(uint)JSI_TT_BOOLEAN },

    { "error",    DBErrorCmd,    0,  0, "", .help="Return the last error on this connection", .retType=(uint)JSI_TT_OBJECT },

    { "begin",    DBBeginCmd,    0,  0, "", .help="Open a transaction", .retType=(uint)JSI_TT_BOOLEAN },
    { "commit",   DBCommitCmd,   0,  0, "", .help="Commit a transaction", .retType=(uint)JSI_TT_BOOLEAN },
    { "rollback", DBRollbackCmd, 0,  0, "", .help="Rollback a transaction", .retType=(uint)JSI_TT_BOOLEAN },

    { "use",      DBUseCmd,      1,  1, "dbname:string", .help="Use a database.", .retType=(uint)JSI_TT_BOOLEAN },

    { "query",    DBQueryCmd,    1,  2, "query:string, params:array", .help="Query the database." }, // TODO: Returns a Query object.

    { "lastSeq",  DBLastSequenceCmd, 0,  1, "seqname:string", .help="Fetch the last sequence ID assigned if supported", .retType=(uint)JSI_TT_NUMBER },
    { "nextSeq",  DBNextSequenceCmd, 0,  1, "seqname:string", .help="Fetch the next sequence ID if supported", .retType=(uint)JSI_TT_NUMBER },

    { "check",    DBCheckCmd,    0,  0, "", .help="Check the server connection", .retType=(uint)JSI_TT_BOOLEAN },
    { "escape",   DBEscapeCmd,   1,  1, "text:string", .help="Escape a string.", .retType=(uint)JSI_TT_STRING },
    { NULL, 0,0,0,0, .help="Commands for interacting with a DB object"  }
};

static Jsi_RC dbObjFree(Jsi_Interp *interp, void *data)
{
    DBObj *cmdPtr = (DBObj *)data;

    if( cmdPtr->db )
        dbi_conn_close( cmdPtr->db );

    //Jsi_EventFree(cmdPtr->interp, cmdPtr->event);
    //Jsi_OptionsFree(cmdPtr->interp, DoorOptions, cmdPtr, 0);
    cmdPtr->interp = NULL;

    // close FDs and crap.
    // not much to 'free' per se.

    //Jsi_ObjFree(interp, cmdPtr->fobj); // TODO Necessary?
    Jsi_Free(cmdPtr);
    return JSI_OK;
}

static bool dbObjIsTrue(void *data)
{
    return true;
}

static bool dbObjEqual(void *data1, void *data2)
{
    return (data1 == data2);
}

static Jsi_UserObjReg dbobject = {
    "DBObject",
    dbCmds,
    dbObjFree,
    dbObjIsTrue,
    dbObjEqual
};

static Jsi_CmdProcDecl(DBConstructor)
{
    DBObj *cmdPtr = (DBObj *)Jsi_Calloc(1, sizeof(DBObj));
    int argc = Jsi_ValueGetLength(interp, args);
    if( argc < 2 || argc > 2 )
        goto bail;

    Jsi_Value *argDriver = Jsi_ValueArrayIndex(interp, args, 0);
    Jsi_Value *argOptions = Jsi_ValueArrayIndex(interp, args, 1);

    if( argDriver == NULL || !Jsi_ValueIsString(interp, argDriver) )
        goto bail;

    const char *constDriver = Jsi_ValueString(interp, argDriver, NULL);
    dbi_conn db = dbi_conn_new_r( constDriver, g_dbi );
    if( !db )
    {
        // TODO: Errstr
        goto bail;
    }

    if( argOptions == NULL || Jsi_ValueIsNull(interp,argOptions) || !Jsi_ValueIsObjType(interp,argOptions, JSI_OT_OBJECT) )
        goto cleanbail;

    // Iterate through provided options, apply them to our new conn:
    Jsi_Value *dbargs = Jsi_ValueNew(interp);
    if( JSI_OK != Jsi_ValueGetKeys(interp, argOptions, dbargs) )
    {
        Jsi_ValueFree(interp, dbargs);
        goto cleanbail;
    }

    int argcnt = Jsi_ValueGetLength(interp, dbargs);
    for( int i=0; i < argcnt; i++ )
    {
        Jsi_Value *v = Jsi_ValueArrayIndex(interp, dbargs, i);
        if (!v) continue;
        const char *cp = Jsi_ValueString(interp, v, 0);
        if (!cp) continue;

        // key is 'cp':
        Jsi_Value *val = Jsi_ValueObjLookup(interp, argOptions, cp, true);
        if( Jsi_ValueIsString(interp, val) )
        {
            // Set as string option:
            const char *vstr = Jsi_ValueString(interp, val, 0);
            dbi_conn_set_option( db, cp, vstr );
        }
        else if( Jsi_ValueIsNumber(interp, val) )
        {
            // Set as numeric option:
            int vnum = Jsi_ValueToNumberInt(interp, val, true);
            dbi_conn_set_option_numeric( db, cp, vnum );
        }
        else
        {
            // TODO Unsupported type.
            Jsi_ValueFree(interp, dbargs);
            goto cleanbail;
        }
    }
    Jsi_ValueFree(interp, dbargs);

    cmdPtr->db = db;

    // Make all the things!
    Jsi_Obj *o = Jsi_ObjNew(interp);
    Jsi_PrototypeObjSet(interp, "DBObject", o);
    Jsi_ValueMakeObject(interp, ret, o);

    cmdPtr->fobj = Jsi_ValueGetObj(interp, *ret);
    if ((cmdPtr->objid = Jsi_UserObjNew(interp, &dbobject, cmdPtr->fobj, cmdPtr))<0)
        goto cleanbail;

    return JSI_OK;

cleanbail:
    dbi_conn_close( db );

bail:
    dbObjFree(interp, cmdPtr);
    Jsi_ValueMakeUndef(interp, ret);
    return JSI_ERROR;
}

static Jsi_CmdSpec dbConstructor[] = {
    { "DB",       DBConstructor, 2,  2, "driver:string, params:object", .help="Create database object and connect via supplied parameters.",
            .retType=(uint)JSI_TT_USEROBJ, .flags=JSI_CMD_IS_CONSTRUCTOR, .info=JSI_INFO("\
Create database object and connect via supplied parameters.") },
    { NULL, 0,0,0,0, .help="The constructor for creating a DB object."  }
};

Jsi_InitProc Jsi_Initdbi;

Jsi_RC Jsi_Releasedbi(Jsi_Interp *interp) {
    dbi_shutdown_r(g_dbi);
    return JSI_OK;
}

Jsi_RC Jsi_Initdbi(Jsi_Interp *interp, int release) {
    if(release) return Jsi_Releasedbi(interp);

    dbi_initialize_r(NULL, &g_dbi);

    // Register DB prototype:
    Jsi_Hash *wsys;
    if (!(wsys = Jsi_UserObjRegister(interp, &dbobject))) {
        Jsi_LogBug("Can not init dbobject");
        return JSI_ERROR;
    }

    // Register Query prototype:
    Jsi_Hash *wsys2;
    if (!(wsys2 = Jsi_UserObjRegister(interp, &dbqueryobject))) {
        Jsi_LogBug("Can not init dbqueryobject");
        return JSI_ERROR;
    }

    Jsi_PkgProvide(interp, "dbi", 1, Jsi_Initdbi);
    Jsi_CommandCreateSpecs(interp, "DB", dbConstructor, NULL, 0);
    Jsi_CommandCreateSpecs(interp, dbobject.name, dbCmds, wsys, JSI_CMDSPEC_ISOBJ);
    Jsi_CommandCreateSpecs(interp, dbqueryobject.name, dbQueryCmds, wsys2, JSI_CMDSPEC_ISOBJ);
    return JSI_OK;
}
