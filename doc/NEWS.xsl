<?xml version='1.0'?>
<!-- NEWS customizations, applied as -m argument of xmlto -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>
<xsl:param name="use.id.as.filename" select="'1'"></xsl:param>
<xsl:param name="chunk.first.sections" select="'1'"></xsl:param>
<xsl:param name="section.autolabel" select="'0'"></xsl:param>
<xsl:param name="generate.index" select="0"></xsl:param>

<xsl:param name="html.stylesheet.type">text/css</xsl:param>
<xsl:param name="html.stylesheet" select="'manual.css'"></xsl:param>

<xsl:param name="draft.mode" select="'no'"></xsl:param>

<xsl:param name="generate.toc"/>
</xsl:stylesheet>
