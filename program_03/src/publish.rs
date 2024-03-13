use std::net::TcpStream;
use std::io::Write;
use std::path::Path;

use crate::error::{self, Context as _};
use crate::types::ActionType;

/// send the publish command to registry
///
/// this will take a variable that can be reference the std::path::Path data
/// type
pub fn send_publish<P>(conn: &mut TcpStream, dir: P) -> error::Result<()>
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
    let dir_contents = std::fs::read_dir(dir)
        .context("failed to read ocntents of shared directory")?;

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
    conn.write_all(&buffer[..wrote_bytes])
        .context("error sending publish to registry")
}
