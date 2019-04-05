# jsidbi
LibDBI interface for JSI

## Building
Edit the Makefile such that JSIDIR points to the src/ directory within your JSI codebase.

libdbi must be installed, along with the developer headers.

Run 'make'.

## Creating a DB instance:
You can copy the resulting 'dbi.so' either to you system modules directory (on my system, "/usr/local/lib/jsi/dbi.so" works) or wherever you like, with Interp.conf({pkgDirs:[]}) configured appropriately.

In your script, load it with:

    require('dbi');

Create a database instance with:

    var db = new DB("mysql", {'host':'localhost', 'username':'root', 'dbname':'test'});

The database type names and parameters accepted by each can be found on the libdbi-drivers documentation.

### db.open():bool
Upon successfully connecting to the database, returns true, otherwise false.

### db.error():object
Returns an object containing the most recent error on the database handle in the format:

    { "number":<code>, "text":<error message> }

### db.begin():bool
Begin a transaction. If successful, returns true. If it failed for any reason (including the underlying database lacking a transaction implementation) it will return false.

### db.commit():bool
Commit a transaction. If successful, returns true. If it failed for any reason (including the underlying database lacking a transaction implementation) it will return false.

### db.rollback():bool
Roll back a transaction. If successful, returns true. If it failed for any reason (including the underlying database lacking a transaction implementation) it will return false.

### db.use(dbname:string):bool
Switch to use the specified database "dbname". If successful, returns true. Otherwise false.

### db.query(query:string, params:array=void):DBQuery
Query the database using the provided query string. Parameters (if any) may be provided in the 'params' array.

Parameters provided are interpolated by their position in their passed array.

Indexes start at 1 for interpolated tokens and are notated by a '%' symbol.

    var q = db.query("SELECT foo, bar FROM mytable WHERE firstname=%1 AND lastname=%2", ["Tricia", "MacMillan"]);

### db.lastSeq(name:string=void):number
Return the last sequence value for the named sequence (if supported).

The last sequence generated (on any table) will be returned for databases which do not support explicit sequences (eg, Mysql)

### db.nextSeq(name:string=void):number
Return the next sequence value for the named sequence (if supported).

For databases which do not support explicit sequences (eg, Mysql) this method is useless.

### db.check():bool
Check if the server is connected and, if not, attempt to reconnect.

Returns true if connected, false otherwise.

### db.escape(content:string):string
Escape a string for inclusion in a SQL query directly.

Returns the escaped string if successful, false otherwise.


## Creating a Query instance:

    var query = db.query("SELECT cheese FROM kitchen WHERE kind != 'mozzarella'");

If a query is successful, a handle is returned which offers relatively basic operations and parameters.

### query.fields:array
An ordered array of the fields returned by the query.

### query.rowcount:number
How many rows are returned by the query.

### query.seek(row:number):bool
Move to a specific row within the result set. If successful, returns true. Otherwise, false.

### query.value(column:number):<mixed>
Return the value at the specified column index in the currently selected row.

    * NULL field values will be returned as a Null value.
    * Integers will be converted to "long long", or a signed 64-bit integer, before being returned as a JSI Number.
    * Floating point and double precision are treated as doubles.
    * Binary data and strings are returned as normal, but be mindful that JSI is (currently) not 100% unicode safe.
    * Datetime will be returned as seconds since UNIX epoch.
