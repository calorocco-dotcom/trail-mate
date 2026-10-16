#pragma once
class LoraBoard
{
  public:
    virtual ~LoraBoard() = default;
    virtual bool isRadioOnline() const = 0;
};
