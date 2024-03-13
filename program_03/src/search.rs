use std::net::{IpAddr, SocketAddr, TcpStream, Ipv4Addr};
use std::io::{Read, Write};

use crate::error::{self, Context as _};
use crate::types::{ActionType, PeerId};
use crate::data;

pub fn search_registry(conn: &mut TcpStream, filename: &[u8]) -> error::Result<Option<(PeerId, SocketAddr)>> {
    let mut search_buf = vec![0u8; filename.len() + 2];
    search_buf[0] = ActionType::SEARCH as u8;
    search_buf[1..=filename.len()].copy_from_slice(filename);

    conn.write_all(&search_buf)
        .context("io error when sending search request")?;

    let mut peer_buf = [0u8; 10];

    conn.read_exact(&mut peer_buf)
        .context("io error when recieving search result")?;

    let peer_id = PeerId::from_be_bytes(data::cp_array(&peer_buf[..=3]));
    let socket_addr = SocketAddr::from((
        IpAddr::from(Ipv4Addr::from(data::cp_array(&peer_buf[4..=7]))),
        u16::from_be_bytes(data::cp_array(&peer_buf[8..]))
    ));

    if peer_id == 0 {
        Ok(None)
    } else {
        Ok(Some((peer_id, socket_addr)))
    }
}

/// sends the search command to registry
pub fn send_search(conn: &mut TcpStream, filename: &str) -> error::Result<()> {
    let ascii_bytes = data::ascii_bytes(filename)
        .context("filename provided contains non ASCII characters")?;

    match search_registry(conn, ascii_bytes)? {
        Some((peer_id, socket_addr)) => {
            println!("File found at\n  Peer {}\n  {}", peer_id, socket_addr);
        }
        None => {
            println!("File not found");
        },
    }

    Ok(())
}
