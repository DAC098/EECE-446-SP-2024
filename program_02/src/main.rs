use std::net::{IpAddr, SocketAddr, TcpStream, Ipv4Addr};
use std::time::Duration;
use std::str::FromStr;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use dns_lookup::{lookup_host};

/// default port for registry server
const DEFAULT_PORT: u16 = 12345;

/// data type alias for the PeerId
type PeerId = u32;

/// action types to send to the server
///
/// we will specify the underlying representation of the enum so we can easily
/// convert it to what we need for network communications
#[repr(u8)]
enum ActionType {
    JOIN = 0,
    PUBLISH = 1,
    SEARCH = 2,
}

/// copies a slice into the specified array size
///
/// since this will just use try_into because there is no actual logic to
/// perform
#[inline]
fn cp_array<const N: usize>(slice: &[u8]) -> [u8; N] {
    slice.try_into().unwrap()
}

/// sends join command to registry
fn send_join(conn: &mut TcpStream, id: PeerId) -> bool {
    // create a fixed sized array of 5 u8 integers initialized to 0
    let mut buffer = [0u8; 5];
    // cast action type to a u8
    buffer[0] = ActionType::JOIN as u8;
    // copy the big endian of id into the buffer starting from index 1
    // to the end
    buffer[1..].copy_from_slice(&id.to_be_bytes());

    // attempt to write the entire buffer to the tcp socket. if there is an
    // error then print to stdout
    if let Err(err) = conn.write_all(&buffer) {
        println!("failed sending join to registry: {}", err);
        false
    } else {
        true
    }
}

/// send the publish command to registry
///
/// this will take a variable that can be reference the std::path::Path data
/// type
fn send_publish<P>(conn: &mut TcpStream, dir: P) -> bool
where
    P: AsRef<Path>
{
    // track the amount of bytes we have written to the buffer
    let mut wrote_bytes = 5usize;
    // track the amout of file names we have written to the buffer
    let mut count = 0u32;
    let mut buffer = vec![0u8; 1200];
    buffer[0] = ActionType::PUBLISH as u8;

    // get an iterator for the specified directory
    let dir_contents = match std::fs::read_dir(dir) {
        Ok(d) => d,
        Err(err) => {
            println!("failed to read contents of shared directory: {}", err);
            return false;
        }
    };

    for result_entry in dir_contents {
        // since each iteration of dir_contents returns a Result we have to
        // make sure that there was no error when trying to get the
        // directory entry
        let entry = match result_entry {
            Ok(e) => e,
            Err(err) => {
                println!("error reading directory entry: {}", err);
                continue;
            }
        };
        let path = entry.path();
        // try to retrieve file metadata since we will need this to check if
        // the file is an actual file for something else
        let metadata = match entry.metadata() {
            Ok(m) => m,
            Err(err) => {
                println!("error reading metadata for file: \"{}\" {}", path.display(), err);
                continue;
            }
        };

        if !metadata.is_file() {
            continue;
        }

        // on the off change that we are unable to get a file name from the
        // path
        let Some(file_name) = path.file_name() else {
            println!("failed to retrieve file name: \"{}\"", path.display());
            continue;
        };

        if !file_name.is_ascii() {
            println!("filename contains non-ascii characters: \"{}\"", path.display());
            continue;
        }

        // refer to rust std::ffi::OsStr::as_encoded_bytes for details on what
        // this function does
        let bytes = file_name.as_encoded_bytes();
        // to account for the null terminated byte at the end of the string
        let bytes_null_len = bytes.len() + 1;

        if bytes_null_len > buffer.len() - wrote_bytes {
            println!("not enough space in buffer for filename: \"{:?}\"", file_name);
            break;
        }

        // copy the contents of bytes into an offset of exact size needed for
        // the string
        buffer[wrote_bytes..(wrote_bytes + bytes.len())].copy_from_slice(bytes);

        wrote_bytes += bytes_null_len;
        count += 1;
    }

    // copy the total count of file strings processed
    buffer[1..5].copy_from_slice(&count.to_be_bytes());

    // send the contents of the buffer upto the amount written
    if let Err(err) = conn.write_all(&buffer[..wrote_bytes]) {
        println!("error sending publish to registry: {}", err);

        false
    } else {
        true
    }
}

/// sends the search command to registry
fn send_search(conn: &mut TcpStream, filename: &str) -> bool {
    let mut search_buf= vec![0u8; filename.len() + 2];
    search_buf[0] = ActionType::SEARCH as u8;
    // a check is made before this point to ensure that the string given is
    // only ascii characters
    search_buf[1..=filename.len()].copy_from_slice(&filename.as_bytes());

    if let Err(err) = conn.write_all(&search_buf) {
        println!("error sending search to registry: {}", err);
        return false;
    }

    let mut peer_buf = [0u8; 10];

    // read in enough data to fill the provided buffer
    if let Err(err) = conn.read_exact(&mut peer_buf) {
        println!("failed receiving search from registry: {}", err);
        return false;
    }

    let peer_id = PeerId::from_be_bytes(cp_array(&peer_buf[..=3]));
    let socket_addr = SocketAddr::from((
        IpAddr::from(Ipv4Addr::from(cp_array(&peer_buf[4..=7]))),
        u16::from_be_bytes(cp_array(&peer_buf[8..]))
    ));

    println!("result:\n  Peer ID: {}\nAddr: {}", peer_id, socket_addr);

    true
}

/// parse a string into a valid SocketAddr
///
/// this will attempt to parse a given string as a valid Ipv4/Ipv6 address
/// with a port, a valid Ipv4/Ipv6, a domain with specified socket, and then
/// just as a valid domain.
///
/// because we don't have direct access to getaddrinfo we are using an imported
/// crate to help with dns lookup.
fn parse_remote(value: &str) -> SocketAddr {
    if let Ok(socket) = SocketAddr::from_str(&value) {
        socket
    } else if let Ok(ip) = IpAddr::from_str(&value) {
        SocketAddr::from((ip, DEFAULT_PORT))
    } else {
        // if the string provided contains a port number split it at the
        // delimiter and parse the port number to a valid u16 integer
        let (domain, port) = if let Some((d, p)) = value.split_once(':') {
            let Ok(valid) = p.parse::<u16>() else {
                panic!("invalid port provided for remote host: \"{}\"", value);
            };

            (d, valid)
        } else {
            (value, DEFAULT_PORT)
        };

        let lookups = lookup_host(domain)
            .expect("failed to lookup remote host");

        // we will take the first Ip address in the list and return that
        let Some(first) = lookups.first() else {
            panic!("unable to resolve remote host: \"{}\"", domain);
        };

        SocketAddr::from((first.clone(), port))
    }
}

/// creates the default registry address
#[inline]
fn default_remote() -> SocketAddr {
    SocketAddr::from(([0,0,0,0], DEFAULT_PORT))
}

/// prints the commands available to the client
fn print_cmds() {
    println!("\
JOIN | join: the client will join the registry with the specified peer id

PUBLISH | publish: the client will send a list of available ilfes in the \
shared directory

SEARCH | search: searches the registry for the specified file name. the \
registry will return a valid ip and port address if it was found or 0 if it \
was not found

EXIT | exit | quit: closes the client

help: prints this help message");
}

fn main() {
    let mut shared_dir_opt: Option<PathBuf> = None;
    let mut remote_opt = None;
    let mut peer_id_opt = None;
    let mut args = std::env::args();
    args.next();

    // on the chance that we need to parse more than one argument from the
    // command line we will just loop until told to break
    loop {
        let Some(arg) = args.next() else {
            break;
        };

        if peer_id_opt.is_none() {
            // parse the peer id first
            let Ok(id) = arg.parse::<PeerId>() else {
                panic!("invalid peer id provided: \"{}\"", arg);
            };

            peer_id_opt = Some(id);
        } else if remote_opt.is_none() {
            // an optional remote host address is next
            remote_opt = Some(parse_remote(&arg));
        } else if shared_dir_opt.is_none() {
            shared_dir_opt = Some(PathBuf::from(arg));
        }
    }

    let peer_id = peer_id_opt.expect("no peer id provided");
    let remote_host = remote_opt.unwrap_or(default_remote());

    // if we are given a shared directory to use then we can run some checks
    // to get an absolute path from the given one
    let shared_dir = if let Some(shared) = shared_dir_opt {
        if shared.is_absolute() {
            shared
        } else {
            let cwd = std::env::current_dir()
                .expect("failed to retrieve current working directory");

            cwd.join(shared)
        }
    } else {
        let cwd = std::env::current_dir()
            .expect("failed to retrieve current working directory");

        let shared = cwd.join("shared_dir");

        shared
    };

    {
        // check to see if the given directory is an actual directory
        let metadata = shared_dir.metadata()
            .expect("failed to retrieve meatadata for shared_dir");

        if !metadata.is_dir() {
            panic!("shared_dir is not a directory");
        }
    }

    // attempt to connect to the registry and timeout after 5 seconds of no
    // connection being made
    let mut conn = match TcpStream::connect_timeout(&remote_host, Duration::from_secs(5)) {
        Ok(c) => c,
        Err(err) => {
            panic!("failed connecting to remote host: {} {}", remote_host, err);
        }
    };

    let stdin = std::io::stdin();
    let mut stdout = std::io::stdout();
    let mut input = String::new();

    let mut joined_registry = false;

    loop {
        stdout.write(b"> ")
            .expect("failed writing to stdout");
        stdout.flush()
            .expect("failed writing to stdout");

        input.clear();

        stdin.read_line(&mut input)
            .expect("failed reading from stdin");

        // trim the given string input and split it on any whitespace
        // characters found in the string
        let mut split = input.trim()
            .split_whitespace();

        // attempt to get the desired command
        let Some(command) = split.next() else {
            println!("no command specified");
            continue;
        };

        // try matching the given command to an action
        match command {
            "join" | "JOIN" => {
                println!("joining registry");

                if !send_join(&mut conn, peer_id) {
                    continue;
                }

                joined_registry = true;
            }
            "publish" | "PUBLISH" => {
                if !joined_registry {
                    println!("join the regsitry first");
                    continue;
                }

                println!("publishing to registry");

                send_publish(&mut conn, &shared_dir);
            }
            "search" | "SEARCH" => {
                if !joined_registry {
                    println!("join the registry first");
                    continue;
                }

                let Some(file_name) = split.next() else {
                    println!("no file name specified");
                    continue;
                };

                // since the registry does not handle (that I know of)
                // non-ascii data we will check to make sure that the string
                // only contains ascii characters
                if !file_name.is_ascii() {
                    println!("the provided file name contains on ASCII characters");
                    continue;
                }

                println!("searching registry");

                send_search(&mut conn, &file_name);
            }
            "help" => {
                print_cmds();
            }
            "quit" | "exit" | "EXIT" => {
                break;
            }
            _ => {
                println!("unknown command provided: \"{}\"", command);
            }
        }
    }
}
