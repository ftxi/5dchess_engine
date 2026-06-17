#ifndef IO_HANDLER_H
#define IO_HANDLER_H
#include <iostream>
#include <string>

class io_handler
{
public:
    virtual std::string read_line() = 0;
    virtual void write_line(const std::string &line) = 0;
    virtual bool is_open() = 0;
    virtual ~io_handler() = default;
};

class stdio_handler : public io_handler
{
public:
    std::string read_line() override;
    void write_line(const std::string &line) override;
    bool is_open() override;
};

#endif /* IO_HANDLER_H */
