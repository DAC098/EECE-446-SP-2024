use std::net::{TcpStream, TcpListener};
use std::time::Duration;

use crate::error::{self, Context as _};
use crate::types::{ActionType, ResponseType};

fn send_response(mut stream: &TcpStream, response: ResponseType) -> error::Result<()> {
    stream.write_all(&[response as u8])
        .context("error sending response to client")
}

struct PeerAction {
    Fetch(String)
}

fn get_action(stream: &mut TcpStream) -> error::Result<Option<PeerAction>> {
    let mut buffer = [0u8; 2048];

    let mut read = stream.read(&mut buffer)
        .context("failed to read peer action")?;

    if read == 0 {
        return Ok(None);
    }

    let Ok(action_type): Result<ActionType, _> = buffer[0].try_into() else {
        send_response(&mut stream, ResponseType::UNKNOWN_ACTION)?;

        return Err(error::Error::new("unknown action received from peer"));
    };

    match action_type {
        ActionType::FETCH => {
            let mut collected = vec![0u8; 100];
            let mut collected_index = 0;
            let mut offset = 1;

            'recv_loop: loop {
                for index in offset..read {
                    if collected_index == collected.len() {
                        send_response(&mut stream, ResponseType::TOO_MUCH_DATA)?;

                        return Err(error::Error::new("too much data received from client"));
                    }

                    // if found null character
                    if buffer[index] == 0 {
                        if index != read - 1 {
                            send_response(&mut stream, ResponseType::TOO_MUCH_DATA)?;

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

            let Ok(filename) = String::from_utf8(&collected) else {
                send_response(stream, ResponseType::INVALID_DATA)?;

                return Err(error::Error::new("invalid string received from peer"));
            };

            Ok(PeerAction::Fetch(filename))
        }
        _ => {
            send_response(stream, ResponseType::UNHANDLED_ACTION)?;

            Err(error::Error::new("unhandled action received from peer"))
        }
    }
}

fn handle_fetch(stream: &mut TcpStream, filename: String) -> error::Result<()> {
    Ok(())
}

fn handle_conn(mut stream: TcpStream) -> error::Result<()> {
    let action = get_action(&mut stream)?;

    match action {
        PeerAction::Fetch(filename) => handle_fetch(&mut stream, filename)
    }
}

pub fn handle_listener(mut listener: TcpListener) {
    loop {
        let (stream, addr) = match listener.accept() {
            Ok(conn) => conn,
            Err(err) => {
                println!("connection error: {}", err);
                continue;
            }
        };

        if let Err(err) = stream.set_read_timeout(Duration::from_secs(3)) {
            println!("failed to set read timeout for client: {}", err);
            continue;
        }

        if let Err(err) = stream.set_write_timeout(Duration::from_secs(3)) {
            println!("failed to set write timeout for client: {}", err);
            continue;
        }

        if let Err(err) = handle_conn(stream) {
            println!("peer error: {}", err);
        }
    }
}
