#!/usr/bin/env jsish

Interp.conf({pkgDirs:['.']});
require('dbi');

puts("Creating DB...");
var x = new DB("mysql", {'host':'localhost', 'port':3306, 'username':'acer', 'password':'aoe123AOE', 'dbname':'brigantine'});
puts("Done. Connecting to server...");
puts( x.open() );
puts( "Error state:" );
puts( x.error() );
puts( "Pinging..." );
puts( x.check() );
puts( "Escaping..." );
var esc = x.escape("%%Lay's%%");
puts("Escaped: \""+esc+"\". Querying...");
var q = x.query("SELECT * FROM barcodes WHERE sku LIKE %1 OR sku LIKE "+esc, ['%591%']);
puts( q );
if( q )
{
	puts( "Fields: "+ JSON.stringify(q.fields) );
	puts( "Rowcount: "+q.rowcount);
	puts( q.toArray( {'first':0, 'last':20} ) );
	puts( q.toArray( {'first':10, 'last':20} ) );
	puts( "Seeking..." );
	puts( q.seek(14) );
	puts( "Cool. What's here?" );
	puts( q.value(1) );
	puts("Done. Destroying query...");
	delete q;
}
else
	puts( x.error() );
puts("Switch database...");
puts( x.use('imthing') );
puts("Done. Destroying DB...");
delete x;
puts("Done.");

System.update();

