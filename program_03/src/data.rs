/// copies a slice into the specified array size
///
/// since this will just use try_into because there is no actual logic to
/// perform
#[inline]
pub fn cp_array<const N: usize>(slice: &[u8]) -> [u8; N] {
    slice.try_into().unwrap()
}

/// checks if the given string as ASCII and then returns bytes
pub fn ascii_bytes<'a>(given: &'a str) -> Option<&'a [u8]> {
    if given.is_ascii() {
        Some(given.as_bytes())
    } else {
        None
    }
}
