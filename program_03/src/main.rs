use std::net::{IpAddr, SocketAddr, TcpStream};
use std::time::Duration;
use std::str::FromStr;
use std::io::Write;
use std::path::PathBuf;

use dns_lookup::{lookup_host};

mod error;
mod data;
mod types;
mod join;
mod publish;
mod search;
mod fetch;

use types::{DEFAULT_PORT, PeerId};

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

FETCH | fetch: performs a SEARCH for the specified file and will then attempt \
to retrieve that file from the specified peer.

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
                if let Err(err) = join::send_join(&mut conn, peer_id) {
                    println!("{}", err);
                } else {
                    joined_registry = true;
                }
            }
            "publish" | "PUBLISH" => {
                if !joined_registry {
                    println!("join the regsitry first");
                    continue;
                }

                if let Err(err) = publish::send_publish(&mut conn, &shared_dir) {
                    println!("{}", err);
                }
            }
            "search" | "SEARCH" => {
                if !joined_registry {
                    println!("join the registry first");
                    continue;
                }

                let Some(filename) = split.next() else {
                    println!("no file name specified");
                    continue;
                };

                if let Err(err) = search::send_search(&mut conn, &filename) {
                    println!("{}", err);
                }
            }
            "fetch" | "FETCH" => {
                if !joined_registry {
                    println!("join the registry first");
                    continue;
                }

                let Some(filename) = split.next() else {
                    println!("no file name specified");
                    continue;
                };

                if let Err(err) = fetch::send_fetch(&mut conn, &filename) {
                    println!("{}", err);
                }
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
