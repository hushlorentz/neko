#ifndef CLOCKED_COMPONENT_HPP
#define CLOCKED_COMPONENT_HPP

class ClockedComponent
{
  public:
    virtual ~ClockedComponent() = default;
    virtual bool clockActive() const = 0;
    virtual void clock() = 0;
};

#endif
