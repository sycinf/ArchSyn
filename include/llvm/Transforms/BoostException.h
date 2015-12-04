namespace boost{
    inline void throw_exception(std::exception const & e)
    {
        errs()<<"boost exception";
        exit(1);
    }
}
