use std::net::{IpAddr, SocketAddr, TcpStream};
use std::time::Duration;
use std::str::FromStr;
use std::io::Write;
use std::path::{Path, PathBuf};

use dns_lookup::{lookup_host};

const DEFAULT_PORT: u16 = 12345;

type PeerId = u32;

#[repr(u8)]
enum ActionType {
    JOIN = 0,
    PUBLISH = 1,
    SEARCH = 2,
}

fn send_join(conn: &mut TcpStream, id: PeerId) -> bool {
    if let Err(err) = conn.write(&[ActionType::JOIN as u8]) {
        println!("failed to write data to registry: {}", err);
        return false;
    }

    let id_bytes = id.to_be_bytes();

    if let Err(err) = conn.write_all(&id_bytes) {
        println!("failed to write data to registry: {}", err);
        false
    } else {
        true
    }
}

fn send_publish<P>(conn: &mut TcpStream, dir: P) -> bool
where
    P: AsRef<Path>
{
    let mut wrote_bytes = 5usize;
    let mut count = 0u32;
    let mut buffer: Vec<u8> = vec![0u8; 1200];
    buffer[0] = ActionType::PUBLISH as u8;

    let dir_contents = match std::fs::read_dir(dir) {
        Ok(d) => d,
        Err(err) => {
            println!("failed to read contents of shared directory: {}", err);
            return false;
        }
    };

    for result_entry in dir_contents {
        let entry = match result_entry {
            Ok(e) => e,
            Err(err) => {
                println!("error reading directory entry: {}", err);
                continue;
            }
        };
        let path = entry.path();
        let metadata = match entry.metadata() {
            Ok(m) => m,
            Err(err) => {
                println!("error reading metadata for file: \"{}\"", path.display());
                continue;
            }
        };

        if !metadata.is_file() {
            continue;
        }

        let Some(file_name) = path.file_name() else {
            continue;
        };

        let bytes = file_name.as_encoded_bytes();
        let bytes_len = bytes.len() + 1;

        if bytes_len > buffer.len() - wrote_bytes {
            println!("not enough space in buffer for filename: \"{:?}\"", file_name);
            break;
        }

        buffer[wrote_bytes..(wrote_bytes + bytes.len())].copy_from_slice(bytes);

        println!("wrote: {:?}", &buffer[wrote_bytes..(wrote_bytes + bytes_len)]);

        count += 1;
        wrote_bytes += bytes_len;

    }

    buffer[1..=4].copy_from_slice(&count.to_be_bytes());

    println!("count: {}", count);
    println!("buffer: {:?}", &buffer[..wrote_bytes]);

    if let Err(err) = conn.write_all(&buffer[..wrote_bytes]) {
        return false;
    }

    true
}

fn parse_remote(value: &str) -> Option<SocketAddr> {
    if let Ok(socket) = SocketAddr::from_str(&value) {
        Some(socket)
    } else if let Ok(ip) = IpAddr::from_str(&value) {
        Some(SocketAddr::from((ip, DEFAULT_PORT)))
    } else {
        let (domain, port) = if let Some((d, p)) = value.split_once(':') {
            let Ok(valid) = p.parse::<u16>() else {
                panic!("invalid port provided for remote host: \"{}\"", value);
            };

            (d, valid)
        } else {
            (value, DEFAULT_PORT)
        };

        let lookups = lookup_host(domain).expect("failed to lookup remote host");

        let Some(first) = lookups.first() else {
            panic!("unable to resolve remote host: \"{}\"", domain);
        };

        Some(SocketAddr::from((first.clone(), port)))
    }
}

#[inline]
fn default_remote() -> SocketAddr {
    SocketAddr::from(([0,0,0,0], DEFAULT_PORT))
}

fn main() {
    let mut shared_dir_opt: Option<PathBuf> = None;
    let mut remote_opt = None;
    let mut peer_id_opt = None;
    let mut args = std::env::args();
    args.next();

    loop {
        let Some(arg) = args.next() else {
            break;
        };

        if peer_id_opt.is_none() {
            let Ok(id) = arg.parse::<PeerId>() else {
                panic!("invalid peer id provided: \"{}\"", arg);
            };

            peer_id_opt = Some(id);
        } else if remote_opt.is_none() {
            remote_opt = parse_remote(&arg);
        }
    }

    let peer_id = peer_id_opt.expect("no peer id provided");
    let remote_host = remote_opt.unwrap_or(default_remote());

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
        let metadata = shared_dir.metadata()
            .expect("failed to retrieve meatadata for shared_dir");

        if !metadata.is_dir() {
            panic!("shared_dir is not a directory");
        }
    }

    let mut conn = match TcpStream::connect_timeout(&remote_host, Duration::from_secs(5)) {
        Ok(c) => c,
        Err(err) => {
            panic!("failed connecting to remote host: {} {}", remote_host, err);
        }
    };

    println!("connected to remote host");

    let mut stdout = std::io::stdout();
    let mut stdin = std::io::stdin();
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

        let mut split = input.trim()
            .split_whitespace();

        let Some(command) = split.next() else {
            println!("no command specified");
            continue;
        };

        match command {
            "join" => {
                println!("joining registry");

                if !send_join(&mut conn, peer_id) {
                    continue;
                }

                joined_registry = true;
            },
            "publish" => {
                if !joined_registry {
                    println!("join the regsitry first");
                    continue;
                }

                println!("publishing to registry");

                send_publish(&mut conn, &shared_dir);
            },
            "search" => {
                if !joined_registry {
                    println!("join the registry first");
                    continue;
                }

                println!("searching registry");
            },
            "help" => {
                println!("available commands");
            }
            "quit" => {
                break;
            },
            _ => {
                println!("unknown command provided: \"{}\"", input);
            }
        }
    }
}
