use std::net::{TcpStream, TcpListener};
use std::time::Duration;
use std::path::{Path, PathBuf, Component};
use std::fs::OpenOptions;
use std::io::{Write, Read};
use std::sync::Arc;
use std::sync::atomic::{Ordering, AtomicBool};

use crate::error::{self, Context as _};
use crate::types::{ActionType, ResponseType};

fn send_response(mut stream: &TcpStream, response: ResponseType) -> error::Result<()> {
    stream.write_all(&[response as u8])
        .context("error sending response to client")
}

#[derive(Debug)]
enum PeerAction {
    Fetch(Box<str>)
}

fn get_action(stream: &mut TcpStream) -> error::Result<Option<PeerAction>> {
    let mut buffer = [0u8; 2048];

    let mut read = stream.read(&mut buffer)
        .context("failed to read peer action")?;

    if read == 0 {
        return Ok(None);
    }

    let Ok(action_type): Result<ActionType, _> = buffer[0].try_into() else {
        send_response(stream, ResponseType::UNKNOWN_ACTION)?;

        return Err(error::Error::new("unknown action received from peer"));
    };

    match action_type {
        ActionType::FETCH => {
            let mut collected = [0u8; 100];
            let mut collected_index = 0;
            let mut offset = 1;

            'recv_loop: loop {
                println!("read {} from peer", read);

                for index in offset..read {
                    if collected_index == collected.len() {
                        send_response(stream, ResponseType::TOO_MUCH_DATA)?;

                        return Err(error::Error::new("too much data received from client"));
                    }

                    // if found null character
                    if buffer[index] == 0 {
                        if index != read - 1 {
                            send_response(stream, ResponseType::TOO_MUCH_DATA)?;

                            return Err(error::Error::new("too much data received from client"));
                        }

                        break 'recv_loop;
                    }

                    collected[collected_index] = buffer[index];
                    collected_index += 1;
                }

                read = stream.read(&mut buffer)
                    .context("failed to read fetch request data")?;

                if read == 0 {
                    break;
                }

                offset = 0;
            }

            let Ok(filename) = std::str::from_utf8(&collected[0..collected_index]) else {
                send_response(stream, ResponseType::INVALID_DATA)?;

                return Err(error::Error::new("invalid string received from peer"));
            };

            Ok(Some(PeerAction::Fetch(filename.into())))
        }
        _ => {
            send_response(stream, ResponseType::UNHANDLED_ACTION)?;

            Err(error::Error::new("unhandled action received from peer"))
        }
    }
}

fn handle_fetch(stream: &mut TcpStream, shared_dir: &Path, filename: &str) -> error::Result<()> {
    let requested_filename = Path::new(filename);
    let mut path = shared_dir.to_path_buf();

    for comp in requested_filename.components() {
        match comp {
            Component::Normal(name) => path.push(name),
            Component::CurDir => {},
            _ => {
                send_response(stream, ResponseType::INVALID_DATA)?;

                return Err(error::Error::new("filename contains invalid path data"));
            }
        }
    }

    let mut file = match OpenOptions::new()
        .read(true)
        .open(&path) {
        Ok(f) => f,
        Err(err) => {
            send_response(stream, ResponseType::ERROR)?;

            return Err(error::Error::with("failed to open filename", err));
        }
    };

    let mut total_wrote = 0;
    let mut offset = 1;
    let mut buffer = [0u8; 2048];
    buffer[0] = ResponseType::SUCCESS as u8;

    loop {
        let read = file.read(&mut buffer[offset..])
            .context("failed to read file data")?;

        if read == 0 {
            break;
        }

        stream.write_all(&buffer[0..read])
            .context("failed to write file data")?;

        offset = 0;
        total_wrote += read;
    }

    println!("total data written to peer: {}", total_wrote);

    Ok(())
}

fn handle_conn(mut stream: TcpStream, shared_dir: &Path) -> error::Result<()> {
    let Some(action) = get_action(&mut stream)? else {
        return Ok(());
    };

    println!("peer action: {:#?}", action);

    match action {
        PeerAction::Fetch(filename) => handle_fetch(&mut stream, shared_dir, &filename)?
    }

    Ok(())
}

pub fn handle_listener(listener: TcpListener, shared_dir: Box<Path>, accept_conn: Arc<AtomicBool>) {
    if let Err(err) = listener.set_nonblocking(true) {
        println!("failed to set nonblocking for listener");
    }

    while accept_conn.load(Ordering::Relaxed) {
        let (stream, addr) = match listener.accept() {
            Ok(conn) => conn,
            Err(ref err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                std::thread::sleep(Duration::from_millis(10));
                continue;
            }
            Err(err) => {
                println!("connection error: {}", err);
                break;
            }
        };

        println!("peer connection: {}", addr);

        if let Err(err) = stream.set_nonblocking(false) {
            println!("failed to set nonblocking for peer: {}", err);
            continue;
        }

        if let Err(err) = stream.set_read_timeout(Some(Duration::from_secs(3))) {
            println!("failed to set read timeout for client: {}", err);
            continue;
        }

        if let Err(err) = stream.set_write_timeout(Some(Duration::from_secs(3))) {
            println!("failed to set write timeout for client: {}", err);
            continue;
        }

        if let Err(err) = handle_conn(stream, &shared_dir) {
            println!("peer error: {}", err);
        }
    }

    println!("listener thread done");
}
