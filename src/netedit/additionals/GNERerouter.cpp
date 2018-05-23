/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2001-2018 German Aerospace Center (DLR) and others.
// This program and the accompanying materials
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
// SPDX-License-Identifier: EPL-2.0
/****************************************************************************/
/// @file    GNERerouter.cpp
/// @author  Pablo Alvarez Lopez
/// @date    Nov 2015
/// @version $Id$
///
//
/****************************************************************************/

// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <string>
#include <iostream>
#include <utility>
#include <utils/geom/GeomConvHelper.h>
#include <utils/geom/PositionVector.h>
#include <utils/common/RandHelper.h>
#include <utils/common/SUMOVehicleClass.h>
#include <utils/common/ToString.h>
#include <utils/geom/GeomHelper.h>
#include <utils/gui/windows/GUISUMOAbstractView.h>
#include <utils/gui/windows/GUIAppEnum.h>
#include <utils/gui/images/GUITextureSubSys.h>
#include <utils/gui/globjects/GUIGLObjectPopupMenu.h>
#include <utils/gui/div/GLHelper.h>
#include <utils/gui/windows/GUIAppEnum.h>
#include <utils/gui/images/GUITexturesHelper.h>
#include <utils/xml/SUMOSAXHandler.h>
#include <netedit/dialogs/GNERerouterDialog.h>
#include <netedit/netelements/GNELane.h>
#include <netedit/netelements/GNEEdge.h>
#include <netedit/changes/GNEChange_Attribute.h>
#include <netedit/GNEViewNet.h>
#include <netedit/GNEUndoList.h>
#include <netedit/GNENet.h>
#include <netedit/GNEViewParent.h>

#include "GNERerouter.h"
#include "GNERerouterInterval.h"


// ===========================================================================
// member method definitions
// ===========================================================================

GNERerouter::GNERerouter(const std::string& id, GNEViewNet* viewNet, const Position &pos, const std::vector<GNEEdge*> &edges, const std::string& filename, double probability, bool off, double timeThreshold, bool blockMovement) :
    GNEAdditional(id, viewNet, GLO_REROUTER, SUMO_TAG_REROUTER, ICON_REROUTER, true, blockMovement, edges),
    myPosition(pos),
    myFilename(filename),
    myProbability(probability),
    myOff(off),
    myTimeThreshold(timeThreshold) {
}


GNERerouter::~GNERerouter() {
}


void
GNERerouter::updateGeometry() {
    // Clear shape
    myShape.clear();

    // Set block icon position
    myBlockIconPosition = myPosition;

    // Set block icon offset
    myBlockIconOffset = Position(-0.5, -0.5);

    // Set block icon rotation, and using their rotation for draw logo
    setBlockIconRotation();

    // Set position
    myShape.push_back(myPosition);

    // update connection positions
    updateChildConnections();

    // Refresh element (neccesary to avoid grabbing problems)
    myViewNet->getNet()->refreshElement(this);
}


Position
GNERerouter::getPositionInView() const {
    return myPosition;
}


void
GNERerouter::openAdditionalDialog() {
    // Open rerouter dialog
    GNERerouterDialog(this);
}


void
GNERerouter::moveGeometry(const Position& oldPos, const Position& offset) {
    // restore old position, apply offset and update Geometry
    myPosition = oldPos;
    myPosition.add(offset);
    updateGeometry();
}


void
GNERerouter::commitGeometryMoving(const Position& oldPos, GNEUndoList* undoList) {
    undoList->p_begin("position of " + toString(getTag()));
    undoList->p_add(new GNEChange_Attribute(this, SUMO_ATTR_POSITION, toString(myPosition), true, toString(oldPos)));
    undoList->p_end();
}


void
GNERerouter::writeAdditional(OutputDevice& device) const {
    // Write parameters
    device.openTag(getTag());
    writeAttribute(device, SUMO_ATTR_ID);
    writeAttribute(device, SUMO_ATTR_EDGES);
    writeAttribute(device, SUMO_ATTR_PROB);
    if (!myFilename.empty()) {
        writeAttribute(device, SUMO_ATTR_FILE);
    }
    if (myTimeThreshold > 0) {
        writeAttribute(device, SUMO_ATTR_HALTING_TIME_THRESHOLD);
    }
    writeAttribute(device, SUMO_ATTR_OFF);
    writeAttribute(device, SUMO_ATTR_POSITION);

    // write intervals and their values
    for (auto i : myRerouterIntervals) {
        i->writeRerouterInterval(device);
    }
    // write block movement attribute only if it's enabled
    if (myBlockMovement) {
        writeAttribute(device, GNE_ATTR_BLOCK_MOVEMENT);
    }
    // Close tag
    device.closeTag();
}


void
GNERerouter::addRerouterInterval(GNERerouterInterval* rerouterInterval) {
    auto it = std::find(myRerouterIntervals.begin(), myRerouterIntervals.end(), rerouterInterval);
    if (it == myRerouterIntervals.end()) {
        myRerouterIntervals.push_back(rerouterInterval);
        // sort intervals always after a adding/restoring
        sortIntervals();
    } else {
        throw ProcessError("Rerouter Interval already exist");
    }
}


void
GNERerouter::removeRerouterInterval(GNERerouterInterval* rerouterInterval) {
    auto it = std::find(myRerouterIntervals.begin(), myRerouterIntervals.end(), rerouterInterval);
    if (it != myRerouterIntervals.end()) {
        myRerouterIntervals.erase(it);
        // sort intervals always after a adding/restoring
        sortIntervals();
    } else {
        throw ProcessError("Rerouter Interval doesn't exist");
    }
}


const std::vector<GNERerouterInterval*>&
GNERerouter::getRerouterIntervals() const {
    return myRerouterIntervals;
}


int
GNERerouter::getNumberOfOverlappedIntervals() const {
    int numOverlappings = 0;
    // iterate over intervals to save the number of overlappings
    for (int i = 0; i < (int)(myRerouterIntervals.size() - 1); i++) {
        if (myRerouterIntervals.at(i)->getEnd() > myRerouterIntervals.at(i + 1)->getBegin()) {
            numOverlappings++;
        } else if (myRerouterIntervals.at(i)->getEnd() > myRerouterIntervals.at(i + 1)->getEnd()) {
            numOverlappings++;
        }
    }
    // return number of overlappings found
    return numOverlappings;
}


void
GNERerouter::sortIntervals() {
    // declare a vector to keep sorted intervals
    std::vector<GNERerouterInterval*> sortedIntervals;
    // sort intervals usin begin as criterium
    while (myRerouterIntervals.size() > 0) {
        int begin_small = 0;
        // find the interval with the small begin
        for (int i = 0; i < (int)myRerouterIntervals.size(); i++) {
            if (myRerouterIntervals.at(i)->getBegin() < myRerouterIntervals.at(begin_small)->getBegin()) {
                begin_small = i;
            }
        }
        // add it to sortd intervals and remove it from myRerouterIntervals
        sortedIntervals.push_back(myRerouterIntervals.at(begin_small));
        myRerouterIntervals.erase(myRerouterIntervals.begin() + begin_small);
    }
    // restore myRerouterIntervals using sorted intervals
    myRerouterIntervals = sortedIntervals;
}


const std::string&
GNERerouter::getParentName() const {
    return myViewNet->getNet()->getMicrosimID();
}


void
GNERerouter::drawGL(const GUIVisualizationSettings& s) const {
    // Start drawing adding an gl identificator
    glPushName(getGlID());

    // Add a draw matrix for drawing logo
    glPushMatrix();
    glTranslated(myShape[0].x(), myShape[0].y(), getType());


    // Draw icon depending of detector is selected and if isn't being drawn for selecting
    if(s.drawForSelecting) {
        GLHelper::setColor(RGBColor::RED);
        GLHelper::drawBoxLine(Position(0, 1), 0, 2, 1);
    } else {
        glColor3d(1, 1, 1);
        glRotated(180, 0, 0, 1);
        if (isAttributeCarrierSelected()) {
            GUITexturesHelper::drawTexturedBox(GUITextureSubSys::getTexture(GNETEXTURE_REROUTERSELECTED), 1);
        } else {
            GUITexturesHelper::drawTexturedBox(GUITextureSubSys::getTexture(GNETEXTURE_REROUTER), 1);
        }
    }

    // Pop draw matrix
    glPopMatrix();

    // Only lock and childs if isn't being drawn for selecting
    if(!s.drawForSelecting) {

        // Show Lock icon depending of the Edit mode
        drawLockIcon(0.4);

        // Draw symbols in every lane
        const double exaggeration = s.addSize.getExaggeration(s);

        if (s.scale * exaggeration >= 3) {
            // draw rerouter symbol over all lanes
            for (auto i : mySymbolsPositionAndRotation) {
                glPushMatrix();
                glTranslated(i.first.x(), i.first.y(), getType());
                glRotated(-1 * i.second, 0, 0, 1);
                glScaled(exaggeration, exaggeration, 1);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

                glBegin(GL_TRIANGLES);
                glColor3d(1, .8f, 0);
                // base
                glVertex2d(0 - 1.4, 0);
                glVertex2d(0 - 1.4, 6);
                glVertex2d(0 + 1.4, 6);
                glVertex2d(0 + 1.4, 0);
                glVertex2d(0 - 1.4, 0);
                glVertex2d(0 + 1.4, 6);
                glEnd();

                // draw "U"
                GLHelper::drawText("U", Position(0, 2), .1, 3, RGBColor::BLACK, 180);

                // draw Probability
                GLHelper::drawText((toString((int)(myProbability * 100)) + "%").c_str(), Position(0, 4), .1, 0.7, RGBColor::BLACK, 180);

                glPopMatrix();
            }
            glPopName();
        }

        // Draw connections
        drawChildConnections();
    }
    // Pop name
    glPopName();

    // Draw name
    drawName(getCenteringBoundary().getCenter(), s.scale, s.addName);
}


std::string
GNERerouter::getAttribute(SumoXMLAttr key) const {
    switch (key) {
        case SUMO_ATTR_ID:
            return getAdditionalID();
        case SUMO_ATTR_EDGES:
            return parseGNEEdges(myEdgeChilds);
        case SUMO_ATTR_POSITION:
            return toString(myPosition);
        case SUMO_ATTR_FILE:
            return myFilename;
        case SUMO_ATTR_PROB:
            return toString(myProbability);
        case SUMO_ATTR_HALTING_TIME_THRESHOLD:
            return toString(myTimeThreshold);
        case SUMO_ATTR_OFF:
            return toString(myOff);
        case GNE_ATTR_BLOCK_MOVEMENT:
            return toString(myBlockMovement);
        case GNE_ATTR_SELECTED:
            return toString(isAttributeCarrierSelected());
        default:
            throw InvalidArgument(toString(getTag()) + " doesn't have an attribute of type '" + toString(key) + "'");
    }
}


void
GNERerouter::setAttribute(SumoXMLAttr key, const std::string& value, GNEUndoList* undoList) {
    if (value == getAttribute(key)) {
        return; //avoid needless changes, later logic relies on the fact that attributes have changed
    }
    switch (key) {
        case SUMO_ATTR_ID:
        case SUMO_ATTR_EDGES:
        case SUMO_ATTR_POSITION:
        case SUMO_ATTR_FILE:
        case SUMO_ATTR_PROB:
        case SUMO_ATTR_HALTING_TIME_THRESHOLD:
        case SUMO_ATTR_OFF:
        case GNE_ATTR_BLOCK_MOVEMENT:
        case GNE_ATTR_SELECTED:
            undoList->p_add(new GNEChange_Attribute(this, key, value));
            break;
        default:
            throw InvalidArgument(toString(getTag()) + " doesn't have an attribute of type '" + toString(key) + "'");
    }
}


bool
GNERerouter::isValid(SumoXMLAttr key, const std::string& value) {
    switch (key) {
        case SUMO_ATTR_ID:
            return isValidAdditionalID(value);
        case SUMO_ATTR_EDGES:
            if (value.empty()) {
                return false;
            } else {
                return checkGNEEdgesValid(myViewNet->getNet(), value, false);
            }
        case SUMO_ATTR_POSITION:
            return canParse<Position>(value);
        case SUMO_ATTR_FILE:
            return isValidFilename(value);
        case SUMO_ATTR_PROB:
            return canParse<double>(value) && (parse<double>(value) >= 0) && (parse<double>(value) <= 1);
        case SUMO_ATTR_HALTING_TIME_THRESHOLD:
            return canParse<double>(value) && (parse<double>(value) >= 0);
        case SUMO_ATTR_OFF:
            return canParse<bool>(value);
        case GNE_ATTR_BLOCK_MOVEMENT:
            return canParse<bool>(value);
        case GNE_ATTR_SELECTED:
            return canParse<bool>(value);
        default:
            throw InvalidArgument(toString(getTag()) + " doesn't have an attribute of type '" + toString(key) + "'");
    }
}


void
GNERerouter::setAttribute(SumoXMLAttr key, const std::string& value) {
    switch (key) {
        case SUMO_ATTR_ID:
            changeAdditionalID(value);
            break;
        case SUMO_ATTR_EDGES: {
            // remove references of this rerouter in all edge childs
            for (auto i : myEdgeChilds) {
                i->removeAdditionalParent(this);
            }
            // set new edges
            myEdgeChilds = parseGNEEdges(myViewNet->getNet(), value);
            // add references to this rerouter in all newedge childs
            for (auto i : myEdgeChilds) {
                i->addAdditionalParent(this);
            }
            break;
        }
        case SUMO_ATTR_POSITION:
            myPosition = parse<Position>(value);
            break;
        case SUMO_ATTR_FILE:
            myFilename = value;
            break;
        case SUMO_ATTR_PROB:
            myProbability = parse<double>(value);
            break;
        case SUMO_ATTR_HALTING_TIME_THRESHOLD:
            myTimeThreshold = parse<double>(value);
            break;
        case SUMO_ATTR_OFF:
            myOff = parse<bool>(value);
            break;
        case GNE_ATTR_BLOCK_MOVEMENT:
            myBlockMovement = parse<bool>(value);
            break;
        case GNE_ATTR_SELECTED:
            if(parse<bool>(value)) {
                selectAttributeCarrier();
            } else {
                unselectAttributeCarrier();
            }
            break;
        default:
            throw InvalidArgument(toString(getTag()) + " doesn't have an attribute of type '" + toString(key) + "'");
    }
    // After setting attribute always update Geometry
    updateGeometry();
}

/****************************************************************************/
