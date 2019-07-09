//
//  Polaris22MetalFixup.h
//  Polaris22MetalFixup
//
//  Copyright © 2019 osy86. All rights reserved.
//

#ifndef Polaris22MetalFixup_h
#define Polaris22MetalFixup_h

#include <IOKit/IOService.h>

class Polaris22MetalFixup : public IOService {
    OSDeclareDefaultStructors(Polaris22MetalFixup);
public:
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    void doKernelPatch(void (^patchFunc)(void));
private:
};

#endif /* Polaris22MetalFixup_h */
