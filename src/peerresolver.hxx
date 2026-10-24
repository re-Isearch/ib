#ifndef IB_PEERRESOLVER_HXX
#define IB_PEERRESOLVER_HXX

#include "idbobj.hxx"

// Cache field descriptors and retain their mapped tables for one operation.
// Keep the scalar resolver's field order and containing-run selection.
class MAPPED_PEER_RESOLVER : public PEER_RESOLVER
{
public:
  MAPPED_PEER_RESOLVER(IDBOBJ& db, FCACHE& cache, size_t& lastPeerField);
  ~MAPPED_PEER_RESOLVER() override;
  FC Resolve(const FC& hit) override;
  PEER_RESOLVER_STATISTICS GetStatistics() const override;
private:
  struct STATE;
  std::unique_ptr<STATE> State;
};

#endif
