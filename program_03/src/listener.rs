use std::net::{TcpStream, TcpListener};
use std::time::Duration;
use std::path::{Path, Component};
use std::fs::OpenOptions;
use std::io::{Write, Read};
use std::sync::Arc;
use std::sync::atomic::{Ordering, AtomicBool};

use crate::error::{self, Context as _};
use crate::types::{ActionType, ResponseType};

/// send a simple response back to the connected peer
fn send_response(mut stream: &TcpStream, response: ResponseType) -> error::Result<()> {
    stream.write_all(&[response as u8])
        .context("error sending response to client")
}

#[derive(Debug)]
enum PeerAction {
    Fetch(Box<str>)
}

/// parses an incoming request from a connected peer
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
            let mut collected = String::with_capacity(100);
            let mut offset = 1;
            let mut found_8bit_char = false;

            'recv_loop: loop {
                println!("read {} from peer", read);

                for index in offset..read {
                    if collected.len() == collected.capacity() {
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

                    if buffer[index] > 127 {
                        found_8bit_char = true;
                    }

                    collected.push(char::from(buffer[index]));
                }

                read = stream.read(&mut buffer)
                    .context("failed to read fetch request data")?;

                if read == 0 {
                    break;
                }

                offset = 0;
            }

            if found_8bit_char {
                println!("WARNING: ascii string from peer contains values greater than 127");
            }

            Ok(Some(PeerAction::Fetch(collected.into())))
        }
        _ => {
            send_response(stream, ResponseType::UNHANDLED_ACTION)?;

            Err(error::Error::new("unhandled action received from peer"))
        }
    }
}

/// handles the fetch action requested by a peer
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

    if total_wrote == 0 {
        // on the off chance that the file is empty then we will report the
        // success but there is no data
        stream.write_all(&buffer[0..1])
            .context("failed to write file data")?;
    }

    println!("total data written to peer: {}", total_wrote);

    Ok(())
}

/// handles a connected peer socket
///
/// the only valid action that a peer can perform currently is the fetch option
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

/// handles the peer connection listener
pub fn handle_listener(listener: TcpListener, shared_dir: Box<Path>, accept_conn: Arc<AtomicBool>) {
    // we are setting the socket to non-blocking so that it will not block when
    // waiting for a new connection. otherwise the only way to close the
    // listener is to create a new connection just so it will close.
    if let Err(err) = listener.set_nonblocking(true) {
        println!("failed to set nonblocking for listener {err}");
    }

    // this will allow us to close the accepting loop from the parent thread.
    // there are different ways of doing something like this that does not
    // involve atomics but this was quick and "easy". we do not care about
    // the memory ordering so long as we get a value from it.
    while accept_conn.load(Ordering::Relaxed) {
        let (stream, addr) = match listener.accept() {
            Ok(conn) => conn,
            Err(ref err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                // this is the "error" returned from the socket indicating that
                // the thread would have blocked but we set the socket to be
                // non blocking. we will now wait the below amount of time to
                // check for a peer connection
                std::thread::sleep(Duration::from_millis(10));
                continue;
            }
            Err(err) => {
                println!("connection error: {}", err);
                break;
            }
        };

        println!("peer connection: {}", addr);

        // the peer socket will inherit the non blocking flag of the listener
        // socket but we want the peer socket to block so we will disable the
        // non blocking flag
        if let Err(err) = stream.set_nonblocking(false) {
            println!("failed to set nonblocking for peer: {}", err);
            continue;
        }

        // since we do not have a custom timer to work with, we will only want
        // to wait for 3 seconds before having the socket throw an error when
        // reading data from the peer
        if let Err(err) = stream.set_read_timeout(Some(Duration::from_secs(3))) {
            println!("failed to set read timeout for client: {}", err);
            continue;
        }

        // similar to the reading timeout
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
