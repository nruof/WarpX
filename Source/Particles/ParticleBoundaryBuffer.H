/* Copyright 2021 Andrew Myers
 *
 * This file is part of WarpX.
 *
 * License: BSD-3-Clause-LBNL
 */
#ifndef PARTICLEBOUNDARYBUFFER_H_
#define PARTICLEBOUNDARYBUFFER_H_

#include "Particles/ParticleBuffer.H"
#include "Particles/MultiParticleContainer_fwd.H"

#include <vector>

/**
 *  This stores particles that have left / been absorbed by domain and embedded boundaries.
 */
class ParticleBoundaryBuffer
{
public:
    ParticleBoundaryBuffer ();

    int numSpecies() const { return getSpeciesNames().size(); }

    const std::vector<std::string>& getSpeciesNames() const {
        static bool initialized = false;
        if (!initialized)
        {
            amrex::ParmParse pp_particles("particles");
            pp_particles.queryarr("species_names", m_species_names);
            initialized = true;
        }
        return m_species_names;
    }

    void gatherParticles (MultiParticleContainer& mypc,
                          const amrex::Vector<const amrex::MultiFab*>& distance_to_eb);

    void clearParticles ();

    void printNumParticles () const;

    int getNumParticlesInContainer(const std::string species_name, int boundary);

    ParticleBuffer::BufferType<amrex::PinnedArenaAllocator>& getParticleBuffer(const std::string species_name, int boundary);

    constexpr int numBoundaries () const {
        return AMREX_SPACEDIM*2
#ifdef AMREX_USE_EB
            + 1
#endif
            ;
    }

private:
    // over boundary, then number of species
    std::vector<std::vector<ParticleBuffer::BufferType<amrex::PinnedArenaAllocator> > > m_particle_containers;

    // over boundary, then number of species
    std::vector<std::vector<int> > m_do_boundary_buffer;

    mutable std::vector<std::string> m_species_names;
};

#endif /*PARTICLEBOUNDARYBUFFER_H_*/
