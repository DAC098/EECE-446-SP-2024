use std::net::{IpAddr, TcpListener, TcpStream};
use std::io::Write;

use crate::error::{self, Context as _};
use crate::types::{ActionType, PeerId};

/// sends register command to registry
pub fn send_register(conn: &mut TcpStream, id: PeerId) -> error::Result<TcpListener> {
    let listener = TcpListener::bind("0.0.0.0:0")
        .context("failed to create tcp listener")?;

    let local_addr = listener.local_addr()
        .context("failed to retrieve local address for tcp listener")?;

    let mut buffer = [0u8; 11];
    buffer[0] = ActionType::REGISTER as u8;
    buffer[1..=4].copy_from_slice(&id.to_be_bytes());

    match local_addr.ip() {
        IpAddr::V4(ip) => {
            buffer[5..=8].copy_from_slice(&ip.octets());
        }
        IpAddr::V6(_) => {
            return Err(error::Error::new("tcp listener is listening on IPv6 address"));
        }
    };

    buffer[9..].copy_from_slice(&local_addr.port().to_be_bytes());

    conn.write_all(&buffer)
        .context("failed to send register to registry")?;

    Ok(listener)
}
