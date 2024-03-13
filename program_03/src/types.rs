pub const DEFAULT_PORT: u16 = 12345;

pub type PeerId = u32;

/// action types to send to the server
///
/// we will specify the underlying representation of the enum so we can easily
/// convert it to what we need for network communications
#[repr(u8)]
pub enum ActionType {
    JOIN = 0,
    PUBLISH = 1,
    SEARCH = 2,
    FETCH = 3,
}
