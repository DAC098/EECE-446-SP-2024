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
            x if x == ActionType::JOIN as u8 => Ok(ActionType::JOIN),
            x if x == ActionType::PUBLISH as u8 => Ok(ActionType::PUBLISH),
            x if x == ActionType::SEARCH as u8 => Ok(ActionType::SEARCH),
            x if x == ActionType::FETCH as u8 => Ok(ActionType::FETCH),
            x if x == ActionType::REGISTER as u8 => Ok(ActionType::REGISTER),
            _ => Err(InvalidActionType)
        }
    }
}

#[repr(u8)]
pub enum ResponseType {
    SUCCESS = 0,
    ERROR = 1,
    UNKNOWN_ACTION = 2,
    UNHANDLED_ACTION = 3,
    NO_DATA = 4,
    TOO_MUCH_DATA = 5,
    INVALID_DATA = 6,
}

pub struct InvalidResponseType;

impl TryFrom<u8> for ResponseType {
    type Error = InvalidResponseType;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            x if x == ResponseType::SUCCESS as u8 => Ok(ResponseType::SUCCESS),
            x if x == ResponseType::ERROR as u8 => Ok(ResponseType::ERROR),
            x if x == ResponseType::UNKNOWN_ACTION as u8 => Ok(ResponseType::UNKNOWN_ACTION),
            x if x == ResponseType::UNHANDLED_ACTION as u8 => Ok(ResponseType::UNHANDLED_ACTION),
            x if x == ResponseType::NO_DATA as u8 => Ok(ResponseType::NO_DATA),
            x if x == ResponseType::TOO_MUCH_DATA as u8 => Ok(ResponseType::TOO_MUCH_DATA),
            x if x == ResponseType::INVALID_DATA as u8 => Ok(ResponseType::INVALID_DATA),
            _ => Err(InvalidResponseType)
        }
    }
}
