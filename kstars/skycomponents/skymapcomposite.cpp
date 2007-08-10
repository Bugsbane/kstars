/***************************************************************************
                          skymapcomposite.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2005/07/08
    copyright            : (C) 2005 by Thomas Kabelmann
    email                : thomas.kabelmann@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "skymapcomposite.h"

#include <QPolygonF>

#include "Options.h"
#include "kstars.h"
#include "kstarsdata.h"
#include "skymap.h"
#include "starobject.h"
#include "deepskyobject.h"
#include "ksplanet.h"

#include "constellationboundary.h"
#include "constellationboundarypoly.h"
#include "constellationlines.h"
#include "constellationnamescomponent.h"
#include "coordinategrid.h"
#include "customcatalogcomponent.h"
#include "deepskycomponent.h"
#include "equator.h"
#include "ecliptic.h"
#include "horizoncomponent.h"
#include "jupitermoonscomponent.h"
#include "milkyway.h"
#include "solarsystemcomposite.h"
#include "starcomponent.h"
#include "satellitecomposite.h"

//#include "ksfilereader.h"
#include "skymesh.h"
#include "skylabeler.h"

SkyMapComposite::SkyMapComposite(SkyComponent *parent, KStarsData *data) : 
    SkyComposite(parent), m_reindexNum( J2000 )
{
    m_skyLabeler = new SkyLabeler();

    m_skyMesh = new SkyMesh( data, 5 );  // level 5 mesh = 8192 trixels

    m_skyMesh->debug( 0 );               //  1 => print "indexing ..."
                                         //  2 => prints totals too
                                         // 10 => prints detailed lists
                                         // You can also set the debug level of individual
                                         // appendLine() and appendPoly() calls.
    //Add all components
	m_MilkyWay = new MilkyWay( this );
	addComponent( m_MilkyWay );
	//Stars must come before constellation lines
	m_Stars = new StarComponent( this );
	addComponent( m_Stars );

	m_CoordinateGrid = new CoordinateGrid( this );
	addComponent( m_CoordinateGrid );

	m_CBounds = new ConstellationBoundary( this );
	addComponent( m_CBounds );

    m_CBoundsBoundary = m_CBounds->boundaries();
    addComponent( m_CBoundsBoundary );

	m_CLines = new ConstellationLines( this );
	addComponent( m_CLines );

	m_CNames = new ConstellationNamesComponent( this );
	addComponent( m_CNames );

	m_Equator = new Equator( this );
	addComponent( m_Equator );

	m_Ecliptic = new Ecliptic( this );
	addComponent( m_Ecliptic );

	m_Horizon = new HorizonComponent( this );
	addComponent( m_Horizon );

	m_Satellites = new SatelliteComposite( this );
	addComponent( m_Satellites );

	//m_DeepSky = new DeepSkyComponent( this, &Options::showDeepSky, 
	//		&Options::showMessier, &Options::showNGC, &Options::showIC, 
	//		&Options::showOther, &Options::showMessierImages );

    m_DeepSky = new DeepSkyComponent( this );
	addComponent( m_DeepSky );

	//FIXME: can't use Options::showCatalog as visibility fcn, 
	//because it returns QList, not bool
	m_CustomCatalogs = new SkyComposite( this );
	foreach ( const QString &fname, Options::catalogFile() ) 
		m_CustomCatalogs->addComponent( new CustomCatalogComponent( this, fname, false,  &Options::showOther ) );
	
	m_SolarSystem = new SolarSystemComposite( this, data );
	addComponent( m_SolarSystem );

	connect( this, SIGNAL( progressText( const QString & ) ), 
					data, SIGNAL( progressText( const QString & ) ) );
}

SkyMapComposite::~SkyMapComposite()
{
    delete m_skyLabeler;     // These are on the heap to avoid header file hell.
    delete m_skyMesh;
}

void SkyMapComposite::update(KStarsData *data, KSNumbers *num )
{
    //printf("updating SkyMapComposite\n");
	//1. Milky Way
	//m_MilkyWay->update( data, num );
	//2. Coordinate grid
	//m_CoordinateGrid->update( data, num );
	//3. Constellation boundaries
	//m_CBounds->update( data, num );
	//4. Constellation lines
	//m_CLines->update( data, num );
	//5. Constellation names
	m_CNames->update( data, num );
	//6. Equator
	//m_Equator->update( data, num );
	//7. Ecliptic
	//m_Ecliptic->update( data, num );
	//8. Deep sky
	//m_DeepSky->update( data, num );
	//9. Custom catalogs
	m_CustomCatalogs->update( data, num );
	//10. Stars
	//m_Stars->update( data, num );
    //m_CLines->update( data, num );  // MUST follow stars.

	//12. Solar system
	m_SolarSystem->update( data, num );
	//13. Satellites
	m_Satellites->update( data, num );
	//14. Horizon
	m_Horizon->update( data, num );
}

void SkyMapComposite::updatePlanets(KStarsData *data, KSNumbers *num )
{
	m_SolarSystem->updatePlanets( data, num );
}

void SkyMapComposite::updateMoons(KStarsData *data, KSNumbers *num )
{
	m_SolarSystem->updateMoons( data, num );
}

//Reimplement draw function so that we have control over the order of 
//elements, and we can add object labels
//
//The order in which components are drawn naturally determines the 
//z-ordering (the layering) of the components.  Objects which 
//should appear "behind" others should be drawn first.
void SkyMapComposite::draw(KStars *ks, QPainter& psky, double scale)
{
    m_map = ks->map();

    m_Stars->reindex( &m_reindexNum );
    m_CLines->reindex( &m_reindexNum );

    // This ensures that the JIT updates are synchronized for the entire draw
    // cycle so the sky moves as a single sheet.
    ks->data()->syncUpdateIDs();

    float radius = m_map->fov();
    if ( radius > 90.0 ) radius = 90.0;

    SkyPoint* focus = m_map->focus();
    m_skyMesh->aperture( focus, radius + 1.0, DRAW_BUF ); // divide by 2 for testing

    if ( Options::showGrid() || Options::showCBounds() || 
			Options::showEquator() && ! m_skyMesh->isZoomedIn() ) {

        m_skyMesh->index( focus, radius + 1.0, NO_PRECESS_BUF );
    }

    m_skyLabeler->reset( m_map, psky, scale ); 

	//TIMING
	//QTime t;
    //t.start();
	//1. Milky Way
//	t.start();
    m_MilkyWay->draw( ks, psky, scale );
//	kDebug() << QString("Milky Way  : %1 ms").arg( t.elapsed() ) << endl;

	//2. Coordinate grid
//	t.start();
	m_CoordinateGrid->draw( ks, psky, scale );
//	kDebug() << QString("Coord grid : %1 ms").arg( t.elapsed() ) << endl;

	//3. Constellation boundaries
//	t.start();
	m_CBounds->draw( ks, psky, scale );
//	kDebug() << QString("Cons Bound : %1 ms").arg( t.elapsed() ) << endl;

	//4. Constellation lines
//	t.start();
	m_CLines->draw( ks, psky, scale );
//	kDebug() << QString("Cons Lines : %1 ms").arg( t.elapsed() ) << endl;

	//5. Constellation names
//	t.start();
	m_CNames->draw( ks, psky, scale );
//	kDebug() << QString("Cons Names : %1 ms").arg( t.elapsed() ) << endl;

	//6. Equator
//	t.start();
	m_Equator->draw( ks, psky, scale );
//	kDebug() << QString("Equator     : %1 ms").arg( t.elapsed() ) << endl;

	//7. Ecliptic
//	t.start();
	m_Ecliptic->draw( ks, psky, scale );
//	kDebug() << QString("Ecliptic    : %1 ms").arg( t.elapsed() ) << endl;

	//8. Deep sky
//	t.start();
	m_DeepSky->draw( ks, psky, scale );
//	kDebug() << QString("Deep sky    : %1 ms").arg( t.elapsed() ) << endl;

	//9. Custom catalogs
//	t.start();
	m_CustomCatalogs->draw( ks, psky, scale );
//	kDebug() << QString("Custom cat  : %1 ms").arg( t.elapsed() ) << endl;

	//10. Stars
//	t.restart();
	m_Stars->draw( ks, psky, scale );
//	kDebug() << QString("Stars       : %1 ms").arg( t.elapsed() ) << endl;

	//11. Solar system
//	t.start();
	m_SolarSystem->draw( ks, psky, scale );
//	kDebug() << QString("Solar sys   : %1 ms").arg( t.elapsed() ) << endl;

	//12. Satellite tracks
//	t.start();
//SATELLITE_DISABLE
//	m_Satellites->draw( ks, psky, scale );
//	kDebug() << QString("Satellites  : %1 ms").arg( t.elapsed() ) << endl;

	//13. Object name labels
//	t.start();
	ks->map()->drawObjectLabels( labelObjects(), psky, scale );
//	kDebug() << QString("Name labels : %1 ms").arg( t.elapsed() ) << endl;

    m_skyLabeler->draw( ks, psky );

	//14. Horizon (and ground)
//	t.start();
	m_Horizon->draw( ks, psky, scale );
//	kDebug() << QString("Horizon     : %1 ms").arg( t.elapsed() ) << endl;


    // -jbb uncomment these to see trixel outlines:

    psky.setPen(  QPen( QBrush( QColor( "green" ) ), 1, Qt::SolidLine ) );
    m_skyMesh->draw( ks, psky, scale, IN_CONSTELL_BUF );

    //psky.setPen(  QPen( QBrush( QColor( "yellow" ) ), 1, Qt::SolidLine ) );
    //m_skyMesh->draw( ks, psky, scale, OBJ_NEAREST_BUF );

    m_reindexNum = KSNumbers( ks->data()->updateNum()->julianDay() );
}

//Select nearest object to the given skypoint, but give preference 
//to certain object types.
//we multiply each object type's smallest angular distance by the 
//following factors before selecting the final nearest object:
// faint stars = 1.0 (not weighted)
// stars brighter than 4th mag = 0.75
// IC catalog = 0.8
// NGC catalog = 0.6
// "other" catalog = 0.6
// Messier object = 0.5
// custom object = 0.5
// Solar system = 0.25
SkyObject* SkyMapComposite::objectNearest( SkyPoint *p, double &maxrad ) {
	double rTry = maxrad;
	double rBest = maxrad;
	SkyObject *oTry = 0;
	SkyObject *oBest = 0;

    m_skyMesh->aperture( p, maxrad + 1.0, OBJ_NEAREST_BUF);
    //kDebug() << QString("Nearest trixels: %1\n").arg( m_skyMesh->intersectSize());

    if ( Options::showStars() ) {
	    oBest = m_Stars->objectNearest( p, rBest );
	    //reduce rBest by 0.75 for stars brighter than 4th mag
	    if ( oBest && oBest->mag() < 4.0 ) rBest *= 0.75;
    }

    if ( Options::showDeepSky() ) {
	    //m_DeepSky internally discriminates among deepsky catalogs
	    //and renormalizes rTry
	    oTry = m_DeepSky->objectNearest( p, rTry ); 
	    if ( rTry < rBest ) {
		    rBest = rTry;
		    oBest = oTry;
	    }
    }

	rTry = maxrad;
	oTry = m_CustomCatalogs->objectNearest( p, rTry );
	rTry *= 0.5;
	if ( rTry < rBest ) {
		rBest = rTry;
		oBest = oTry;
	}

	rTry = maxrad;
	oTry = m_SolarSystem->objectNearest( p, rTry );
	rTry *= 0.25;
	if ( rTry < rBest ) {
		rBest = rTry;
		oBest = oTry;
	}

	maxrad = rBest;
	return oBest; //will be 0 if no object nearer than maxrad was found

}

bool SkyMapComposite::addNameLabel( SkyObject *o ) {
	if ( !o ) return false;
	labelObjects().append( o );
	return true;
}

bool SkyMapComposite::removeNameLabel( SkyObject *o ) {
	if ( !o ) return false;
	int index = labelObjects().indexOf( o );
	if ( index < 0 ) return false;
	labelObjects().removeAt( index );
	return true;
}

bool SkyMapComposite::addTrail( SkyObject *o ) {
	foreach ( SkyComponent *comp, solarSystem() ) {
		if ( comp->addTrail( o ) ) return true;
	}
	//Did not find object o
	return false;
}

bool SkyMapComposite::hasTrail( SkyObject *o, bool &found ) {
	found = false;
	foreach ( SkyComponent *comp, solarSystem() ) {
		if ( comp->hasTrail( o, found ) ) return true;
		//It's possible we found the object, but it had no trail:
		if ( found ) return false;
	}
	//Did not find object o
	return false;
}

bool SkyMapComposite::removeTrail( SkyObject *o ) {
	foreach ( SkyComponent *comp, solarSystem() ) {
		if ( comp->removeTrail( o ) ) return true;
	}
	//Did not find object o
	return false;
}

void SkyMapComposite::clearTrailsExcept( SkyObject *exOb ) {
	foreach ( SkyComponent *comp, solarSystem() ) {
		comp->clearTrailsExcept( exOb );
	}
}

void SkyMapComposite::setFaintStarMagnitude( float newMag ) {
	m_Stars->setFaintMagnitude( newMag );
}

void SkyMapComposite::setStarColorMode( int newMode ) {
	m_Stars->setStarColorMode( newMode );
}

void SkyMapComposite::setStarColorIntensity( int newIntensity ) {
	m_Stars->setStarColorIntensity( newIntensity );
}

QHash<int, QStringList>& SkyMapComposite::objectNames() {
	return m_ObjectNames;
}

QStringList& SkyMapComposite::objectNames( int type ) {
	return m_ObjectNames[ type ];
}

SkyObject* SkyMapComposite::findByName( const QString &name ) {
	//We search the children in an "intelligent" order (most-used 
	//object types first), in order to avoid wasting too much time 
	//looking for a match.  The most important part of this ordering 
	//is that stars should be last (because the stars list is so long)
	SkyObject *o = 0;
	o = m_SolarSystem->findByName( name );
	if ( o ) return o;
	o = m_DeepSky->findByName( name );
	if ( o ) return o;
	o = m_CustomCatalogs->findByName( name );
	if ( o ) return o;
	o = m_Stars->findByName( name );
	if ( o ) return o;

	return 0;
}


SkyObject* SkyMapComposite::findStarByGenetiveName( const QString name ) {
    return m_Stars->findStarByGenetiveName( name );
}

/**
SkyObject* SkyMapComposite::findStarByGenetiveName( const QString &name ) {
	foreach( SkyObject *s, m_Stars->objectList() ) 
		if ( s->name2() == name || ((StarObject*)s)->gname(false) == name ) 
			return (SkyObject*)s;

	return 0;
}
**/

void SkyMapComposite::addCustomCatalog( const QString &filename, bool (*visibleMethod)() ) {
	m_CustomCatalogs->addComponent( new CustomCatalogComponent( this, filename, false, visibleMethod ) );
}

void SkyMapComposite::removeCustomCatalog( const QString &name ) {
  foreach( SkyComponent *sc, m_CustomCatalogs->components() ) {
    CustomCatalogComponent *ccc = (CustomCatalogComponent*)sc;

    if ( ccc->name() == name ) {
      m_CustomCatalogs->removeComponent( ccc );
      return;
    }
  }
  
  kWarning() << i18n( "Could not find custom catalog component named %1." , name) << endl;
}

void SkyMapComposite::reloadDeepSky( KStarsData *data ) {
	m_DeepSky->clear();
	m_DeepSky->init( data );
}

void SkyMapComposite::reloadAsteroids( KStarsData *data ) {
	m_SolarSystem->reloadAsteroids( data );
}

void SkyMapComposite::reloadComets( KStarsData *data ) {
	m_SolarSystem->reloadComets( data );
}

QString SkyMapComposite::constellationName( SkyPoint *p ) {
	QString name = m_CBoundsBoundary->constellationName( p );
	QString fullname;

	if(m_ConstellationNames.isEmpty()) {
		foreach( SkyObject *p, m_CNames->objectList() ) {
			QString longname = p->name().toLower().replace( 0, 1, p->name().at(0).toUpper());
			if ( longname.contains( " " ) ) {
				int i = longname.indexOf(" ")+1;
				longname.replace( i, 1, longname.at(i).toUpper() );
			}
			m_ConstellationNames[ ( p->name2().toUpper() ) ] = longname;
		}
	}

	fullname = m_ConstellationNames[ name.toUpper() ];
	if( ! fullname.isEmpty() )
		return fullname;
	else
		return name;
}


//bool SkyMapComposite::inConstellation( const QString &name, SkyPoint *p ) {
//	return m_CBounds->inConstellation( name, p );
//}

void SkyMapComposite::emitProgressText( const QString &message ) {
	emit progressText( message );
    qApp->processEvents();         // -jbb: this seemed to make it work.
    //kDebug() << QString("PROGRESS TEXT: %1\n").arg( message );
}

float SkyMapComposite::faintStarMagnitude() const { 
	return m_Stars->faintMagnitude(); 
}

int SkyMapComposite::starColorMode() const { 
	return m_Stars->starColorMode(); 
}

int SkyMapComposite::starColorIntensity() const { 
	return m_Stars->starColorIntensity(); 
}

QList<DeepSkyObject*>& SkyMapComposite::deepSkyObjects() { 
	return m_DeepSky->objectList(); 
}

QList<SkyComponent*> SkyMapComposite::solarSystem() { 
	return m_SolarSystem->components(); 
}

QList<SkyObject*>& SkyMapComposite::constellationNames() { 
	return m_CNames->objectList(); 
}

QList<SkyObject*>& SkyMapComposite::stars() { 
	return m_Stars->objectList(); 
}

QList<SkyObject*>& SkyMapComposite::asteroids() { 
	return m_SolarSystem->asteroids(); 
}

QList<SkyObject*>& SkyMapComposite::comets() { 
	return m_SolarSystem->comets(); 
}

KSPlanet* SkyMapComposite::earth() { 
	return m_SolarSystem->earth(); 
}

QList<SkyComponent*> SkyMapComposite::customCatalogs() { 
	return m_CustomCatalogs->components(); 
}

#include "skymapcomposite.moc"
