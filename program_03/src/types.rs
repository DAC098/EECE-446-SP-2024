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
    REGISTER = 4,
}

pub struct InvalidActionType;

impl TryFrom<u8> for ActionType {
    type Error = InvalidActionType;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            x if value == ActionType::JOIN as u8 => Ok(ActionType::JOIN),
            x if value == ActionType::PUBLISH as u8 => Ok(ActionType::PUBLISH),
            x if value == ActionType::SEARCH as u8 => Ok(ActionType::SEARCH),
            x if value == ActionType::FETCH as u8 => Ok(ActionType::FETCH),
            x if value == ActionType::REGISTER as u8 => Ok(ActionType::REGISTER),
            _ => Err(InvalidActionType)
    }
}

#[repr(u8)]
pub enum ResponseType {
    SUCCESS = 0,
    UNKNOWN_ACTION = 1,
    UNHANDLED_ACTION = 2,
    NO_DATA = 3,
    TOO_MUCH_DATA = 4,
    INVALID_DATA = 5,
}

pub struct InvalidResponseType;

impl TryFrom<u8> for ResponseType {
    type Error = InvalidResponseType;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            x if value == ResponseType::SUCCESS as u8 => Ok(ResponseType::SUCCESS),
            x if value == ResponseType::UNKNOWN_ACTION as u8 => Ok(ResponseType::UNKNOWN_ACTION),
            x if value == ResponseType::UNHANDLED_ACTION as u8 => Ok(ResponseType::UNHANDLED_ACTION),
            x if value == ResponseType::NO_DATA as u8 => Ok(ResponseType::NO_DATA),
            x if value == ResponseType::TOO_MUCH_DATA as u8 => Ok(ResponseType::TOO_MUCH_DATA),
            x if value == ResponseType::INVALID_DATA as u8 => Ok(ResponseType::INVALID_DATA),
            _ => Err(InvalidResponseType)
        }
    }
}
