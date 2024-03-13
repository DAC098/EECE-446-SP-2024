use std::net::TcpStream;
use std::fs::{File, OpenOptions};
use std::time::Duration;
use std::io::{Read, Write};
use std::path::PathBuf;

use crate::error::{self, Context as _};
use crate::data;
use crate::types::ActionType;
use crate::search::search_registry;

#[inline]
fn write_to_file(output_file: &mut File, slice: &[u8]) -> error::Result<()> {
    output_file.write_all(slice)
        .context("failed to write output to file")
}

#[inline]
fn read_from_peer(peer: &mut TcpStream, slice: &mut [u8]) -> error::Result<usize> {
    peer.read(slice)
        .context("io error when fetching file")
}

pub fn send_fetch(conn: &mut TcpStream, filename: &str) -> error::Result<()> {
    let ascii_bytes = data::ascii_bytes(filename)
        .context("filename provided contains non ASCII characters")?;

    let Some((_peer_id, socket_addr)) = search_registry(conn, ascii_bytes)? else {
        return Err(error::Error::new("File not found"));
    };

    let mut output_file = {
        let mut output_path = PathBuf::new();
        output_path.push("./");
        output_path.push(&filename);

        OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(output_path)
            .context("failed to create output file")?
    };

    let mut peer = TcpStream::connect_timeout(&socket_addr, Duration::from_secs(5))
        .with_context(|| format!("failed to connect to peer: {}", socket_addr))?;

    {
        let mut fetch_buf = vec![0u8; ascii_bytes.len() + 2];
        fetch_buf[0] = ActionType::FETCH as u8;
        fetch_buf[1..=ascii_bytes.len()].copy_from_slice(ascii_bytes);

        peer.write_all(&fetch_buf)
            .context("io error when fetching file")?;
    }

    let mut read_buf = [0u8; 2048];

    let read = read_from_peer(&mut peer, &mut read_buf)?;

    if read == 0 {
        return Err(error::Error::new("no data from peer"));
    }

    if read_buf[0] != 0 {
        return Err(error::Error::new(format!(
            "non zero response from peer: {}", read_buf[0]
        )));
    }

    write_to_file(&mut output_file, &read_buf[1..read])?;

    loop {
        let read = read_from_peer(&mut peer, &mut read_buf)?;

        if read == 0 {
            break;
        }

        write_to_file(&mut output_file, &read_buf[..read])?;
    }

    Ok(())
}
