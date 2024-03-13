use std::net::TcpStream;
use std::io::Write;

use crate::error::{self, Context as _};
use crate::types::{ActionType, PeerId};

/// sends join command to registry
pub fn send_join(conn: &mut TcpStream, id: PeerId) -> error::Result<()> {
    // create a fixed sized array of 5 u8 integers initialized to 0
    let mut buffer = [0u8; 5];
    // cast action type to a u8
    buffer[0] = ActionType::JOIN as u8;
    // copy the big endian of id into the buffer starting from index 1
    // to the end
    buffer[1..].copy_from_slice(&id.to_be_bytes());

    // attempt to write the entire buffer to the tcp socket
    conn.write_all(&buffer)
        .context("failed to send join to registry")
}
