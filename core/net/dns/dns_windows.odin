package dns

import "core:strings"
import "core:mem"

import win "core:sys/windows"


// Resolves a hostname to exactly one IPv4 and IPv6 address.
// It's then up to you which one you use.
// Note that which address you pass to `dial` determines the type of the socket you get.
//
// Returns `ok = false` if the host name could not be resolved to any addresses.
//
//
// If hostname is actually a string representation of an IP address, this function
// just parses that address and returns it.
// This allows you to pass a generic endpoint string (i.e: hostname or address) to this function end reliably get
// back the endpoint's IP address.
// e.g:
// ```
// 	// Maybe you got this from a config file, so you
//	// don't know if it's a hostname or address.
//	ep_string := "localhost:9000";
//
//	addr_or_host, port, split_ok := net.split_port(ep_string);
//	assert(split_ok);
//	port = (port == 0) ? 9000 : port; // returns zero if no port in the string.
//
//	// Resolving an address just returns the address.
//	addr4, addr6, resolve_ok := net.resolve(addr_or_host);
//	if !resolve_ok {
//		printf("error: cannot resolve %v\n", addr_or_host);
//		return;
//	}
//	addr := addr4 != nil ? addr4 : addr6; // preferring ipv4.
//	assert(addr != nil); // If resolve_ok, we'll have at least one address.
// ```
//
resolve :: proc(hostname: string, addr_types: bit_set[Addr_Type] = {.Ipv4, .Ipv6}) -> (addr4, addr6: Address, ok: bool) {
	if addr := parse_addr(hostname); addr != nil {
		switch a in addr {
		case Ipv4_Address: addr4 = addr;
		case Ipv6_Address: addr6 = addr;
		}
		ok = true;
		return;
	}

	//
	// DEBUG: Why is 1024 bytes not enough when the entire map in get_dns_records is only 256 bytes?
	//

	// NOTE(tetra): We might not have used temporary storage yet,
	// and get_dns_records uses it by default.
	// Rather than require the user initialize it manually first,
	// we just use a stack-arena here instead.
	// We can do this because the addresses we return are returned by value,
	// so we don't return data from within this arena.
	buf: [4096]byte;
	arena: mem.Arena;
	mem.init_arena(&arena, buf[:]);
	allocator := mem.arena_allocator(&arena);

	if .Ipv4 in addr_types {
		recs, rec_ok := get_dns_records(hostname, .Ipv4, allocator);
		if !rec_ok do return;
		if len(recs) > 0 {
			addr4 = cast(Ipv4_Address) recs[0].(Dns_Record_Ipv4); // address is copied
		}
	}

	if .Ipv6 in addr_types {
		recs, rec_ok := get_dns_records(hostname, .Ipv6, allocator);
		if !rec_ok do return;
		if len(recs) > 0 {
			addr6 = cast(Ipv6_Address) recs[0].(Dns_Record_Ipv6); // address is copied
		}
	}

	ok = addr4 != nil || addr6 != nil;
	return;
}



// TODO: Support SRV records.
Dns_Record_Type :: enum u16 {
    Ipv4 = win.DNS_TYPE_A,    // Ipv4 address.
    Ipv6 = win.DNS_TYPE_AAAA, // Ipv6 address.
    Cname = win.DNS_TYPE_CNAME, // Another host name.
    Txt = win.DNS_TYPE_TEXT,  // Arbitrary binary data or text.
    Ns = win.DNS_TYPE_NS,     // Address of a name server. TODO(tetra): .. Name server for what?
    Mx = win.DNS_TYPE_MX,     // Address and preference priority of a mail exchange server.
}

Dns_Record_Ipv4  :: distinct Ipv4_Address;
Dns_Record_Ipv6  :: distinct Ipv6_Address;
Dns_Record_Cname :: distinct string;
Dns_Record_Text  :: distinct string;
Dns_Record_Ns    :: distinct string;

Dns_Record_Mx :: struct {
	host: string,
	preference: int,
}

Dns_Record :: union {
	Dns_Record_Ipv4,
	Dns_Record_Ipv6,
	Dns_Record_Cname,
	Dns_Record_Text,
	Dns_Record_Ns,
	Dns_Record_Mx,
}

// Performs a recursive DNS query for records of a particular type for the hostname.
//
// This procedure instructs the DNS resolver to recursively perform CNAME requests on our behalf,
// meaning that DNS queries for a hostname will resolve through CNAME records until an
// IP address is reached.
//
// WARNING: This procedure allocates memory for each record returned; deleting just the returned slice is not enough!
// See `destroy_records`.
get_url_records :: proc(hostname: string, type: Dns_Record_Type, allocator := context.allocator) -> (records: []Dns_Record, ok: bool) {
	context.allocator = allocator;

	host_cstr := strings.clone_to_cstring(hostname, context.temp_allocator);
	rec: ^win.DNS_RECORD;
	res := win.DnsQuery_UTF8(host_cstr, u16(type), 0, nil, &rec, nil);
	if res == win.DNS_INFO_NO_RECORDS || res == win.ERROR_INVALID_NAME {
		// NOTE(tetra): ERROR_INVALID_NAME is returned if there are no such CNAME or TXT records???
		ok = true;
		return;
	}
	if res != 0 do return;
	defer win.DnsRecordListFree(rec, 1); // 1 means that we're freeing a list... because the proc name isn't enough.

	count := 0;
	for r := rec; r != nil; r = r.pNext {
		if r.wType != u16(type) do continue; // NOTE(tetra): Should never happen, but...
		count += 1;
	}


	recs := make([dynamic]Dns_Record, 0, count);
	if recs == nil do return; // return no results if OOM.

	for r := rec; r != nil; r = r.pNext {
		if r.wType != u16(type) do continue; // NOTE(tetra): Should never happen, but...

		switch Dns_Record_Type(r.wType) {
		case .Ipv4:
			addr := Ipv4_Address(transmute([4]u8) r.Data.A);
			new_rec := Dns_Record_Ipv4(addr); // NOTE(tetra): value copy
			append(&recs, new_rec);
		case .Ipv6:
			addr := Ipv6_Address(transmute([8]u16be) r.Data.AAAA);
			new_rec := Dns_Record_Ipv6(addr); // NOTE(tetra): value copy
			append(&recs, new_rec);
		case .Cname:
			host := string(r.Data.CNAME);
			new_rec := Dns_Record_Cname(strings.clone(host));
			append(&recs, new_rec);
		case .Txt:
			n := r.Data.TXT.dwStringCount;
			ptr := &r.Data.TXT.pStringArray;
			c_strs := mem.slice_ptr(ptr, int(n));
			for cstr in c_strs {
				s := string(cstr);
				new_rec := Dns_Record_Text(strings.clone(s));
				append(&recs, new_rec);
			}
		case .Ns:
			host := string(r.Data.NS);
			new_rec := Dns_Record_Ns(strings.clone(host));
			append(&recs, new_rec);
		case .Mx:
			// TODO(tetra): Order by preference priority? (Prefer hosts with lower preference values.)
			// Or maybe not because you're supposed to just use the first one that works
			// and which order they're in changes every few calls.
			host := string(r.Data.MX.pNameExchange);
			preference := int(r.Data.MX.wPreference);
			new_rec := Dns_Record_Mx { host       = strings.clone(host),
			                          preference = preference };
			append(&recs, new_rec);
		}
	}

	records = recs[:];
	ok = true;
	return;
}

// `records` slice is also destroyed.
destroy_records :: proc(records: []Dns_Record, allocator := context.allocator) {
	context.allocator = allocator;

	for rec in records {
		switch r in rec {
		case Dns_Record_Ipv4:  // nothing to do
        case Dns_Record_Ipv6:  // nothing to do
        case Dns_Record_Cname:
        	delete(string(r));
        case Dns_Record_Text:
        	delete(string(r));
        case Dns_Record_Ns:
        	delete(string(r));
        case Dns_Record_Mx:
        	delete(r.host);
		}
	}

	delete(records);
}