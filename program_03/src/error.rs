// BoxDynError is wrapper that will allow us to store any data struct that
// implements the std::error::Error trait. because we will not know the size of
// the struct at runtime we will have to use the Box struct in order to store
// it on the heap
type BoxDynError = Box<dyn std::error::Error>;
pub type Result<T> = std::result::Result<T, Error>;

/// the common error type for the client
#[derive(Debug)]
pub struct Error {
    /// contains a message indicating what the error is about
    context: String,

    /// a potential source error
    source: Option<BoxDynError>,
}

impl Error {
    /// creates a new Error with the specified message
    pub fn new<C>(cxt: C) -> Self
    where
        C: Into<String>
    {
        Error {
            context: cxt.into(),
            source: None,
        }
    }
}

// if we want a custom struct to be printed by using print!("{}", obj) then we
// will need to implement this trait. it is also required when implementing
// std::error::Error
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self.source {
            Some(err) => if f.alternate() {
                write!(f, "{} {:#}", self.context, err)
            } else {
                write!(f, "{}", self.context)
            }
            None => write!(f, "{}", self.context)
        }
    }
}

// to ensure that the error struct is compatable with other structs that
// implement Error, out struct will also have to implement it.
//
// this will allow structs/functions that take the std::error::Error trait
// as an argument to use our struct without having to write custom code for
// each
impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.source.as_ref().map(|v| &**v as _)
    }
}

/// a Context trait to assist with Result and Option return values
///
/// this is a custom implementation of the traint from anyhow::Context trait
pub trait Context<T, E> {
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>;

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error>;
}

// this will allow for anything that returns a Result with an error type that
// can be turned into a BoxDynError to be mapped into the custom error
//
// example:
//
// let result = match try_thing() {
//     Ok(rtn) => rtn,
//     Err(err) => {
//         return Err(Error::new("failed to do thing"));
//     }
// };
//
// becomes:
//
// let result = try_thing().context("failed to do thing")?;
impl<T, E> Context<T, E> for std::result::Result<T, E>
where
    E: Into<BoxDynError>
{
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>
    {
        match self {
            Ok(v) => Ok(v),
            Err(err) => Err(Error {
                context: cxt.into(),
                source: Some(err.into()),
            })
        }
    }

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error> {
        match self {
            Ok(v) => Ok(v),
            Err(err) => Err(Error {
                context: cb(),
                source: Some(err.into()),
            })
        }
    }
}

// similar to the above Result implementation but for options
//
// example:
//
// let result = match maybe_thing() {
//     Some(rtn) => rtn,
//     None => {
//         return Err(Error::new("failed to get thing"));
//     }
// };
//
// becomes:
//
// let result = maybe_thing().context("failed to get thing")?;
impl<T> Context<T, ()> for std::option::Option<T> {
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>
    {
        match self {
            Some(v) => Ok(v),
            None => Err(Error {
                context: cxt.into(),
                source: None,
            })
        }
    }

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error> {
        match self {
            Some(v) => Ok(v),
            None => Err(Error {
                context: cb(),
                source: None,
            })
        }
    }
}
